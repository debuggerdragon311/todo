// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

#include <dirent.h>    // dir. fns.
#include <limits.h>    // PATH_MAX 4096
#include <stdbool.h>   // bool
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>     // io
#include <stdlib.h>    // size_t
#include <string.h>    // string ops.
#include <sys/stat.h>  // file stats
#include <sys/types.h> // file access
#include <unistd.h>    // getcwd()

#define LOG_IMPLEMENTATION
#include "log.h"

////////////////////////////////
// Enums (Zero heap overhead)
typedef enum TodoState {
  TODO_STATE_OPEN = 0, // Default
  TODO_STATE_IN_PROGRESS,
  TODO_STATE_BLOCKED,
  TODO_STATE_DONE
} todo_state_t;

typedef enum TodoKind {
  TODO_KIND_TODO = 0,
  TODO_KIND_FIXME,
  TODO_KIND_NOTE,
  TODO_KIND_HACK
} todo_kind_t;

////////////////////////////////
// meta (location) object
/*
 * TODO:(#4) Add file-type enumeration for language-aware parsing:
 *       FT_C_LIKE (//, / *...* /), FT_SHELL_LIKE (#), FT_SQL_LIKE (--)
 *       => add extra fields
 *           - file types
 *           - lang. awareness
 *           - ...
 */
typedef struct Location {
  const char *file_path;
  uint32_t line_number;
  uint32_t column_number;
} location_t;

////////////////////////////////
// todo object -> tasks (root)
typedef struct Todo {
  int64_t ref_id;     // -1 if omitted (triggers auto-generation)
  uint8_t priority;   // 0 - 100 (e.g. 50 default)
  todo_state_t state; // Enum instead of char*
  todo_kind_t kind;   // TODO vs FIXME vs NOTE
  location_t loc;     // File origin

  char *title;       // Heap-allocated single line
  char *description; // Heap-allocated multiline block (or NULL if omitted)
} todo_t;

////////////////////////////////
// DArray for paths obj.
typedef struct Storage {
  char **item;
  size_t count;
  size_t capacity;
} path_db;

// Add path to the end
void pdb_push(path_db *db, const char *item) {
  if (!db || !item) {
    return;
  }

  // Capacity expansion
  if (db->count == db->capacity) {
    size_t new_cap =
        db->capacity == 0 ? 8 : (size_t)(db->capacity * 1.61803398875);

    // Guard against truncation with small numbers
    if (new_cap <= db->capacity) {
      new_cap = db->capacity + 1;
    }

    char **new_item = realloc(db->item, new_cap * sizeof(*new_item));

    if (new_item == NULL) {
      perror("realloc");
      exit(EXIT_FAILURE);
    }

    db->item = new_item;
    db->capacity = new_cap;
  }

  // Store heap-allocated copy so stack buffers (e.g., path[PATH_MAX]) stay
  // valid
  char *dup = strdup(item);
  if (!dup) {
    perror("strdup");
    exit(EXIT_FAILURE);
  }
  db->item[db->count++] = dup;
}

// remove path from end
// gives ownership of string pointer to caller via *item
int pdb_pop(path_db *db, char **item) {
  if (!db || db->count == 0) {
    return 0;
  }
  db->count--;
  if (item != NULL) {
    *item = db->item[db->count];
  } else {
    free(db->item[db->count]);
  }
  return 1;
}

// Return last path string without removing
// NOTE: Returned pointer is owned by the DB. Do not free it 
// and do not use it after calling pdb_pop() or pdb_free().
char *pdb_top(const path_db *db) {
  if (!db || db->count == 0) {
    return NULL;
  }
  return db->item[db->count - 1];
}

// Access path string at index
char *pdb_get(const path_db *db, size_t index) {
  if (!db || index >= db->count) {
    return NULL;
  }
  return db->item[index];
}

// Clean up allocated memory
void pdb_free(path_db *db) {
  if (!db)
    return;

  for (size_t i = 0; i < db->count; i++) {
    free(db->item[i]);
  }
  free(db->item);
  db->item = NULL;
  db->count = 0;
  db->capacity = 0;
}

////////////////////////////////
// DArray for Todo storage
typedef struct TodoDB {
  todo_t *items;
  size_t count;
  size_t capacity;
} todo_db;

// Add todo items to the end
void tdb_push(todo_db *db, const todo_t *item) {
  if (!db || !item) {
    return;
  }

  // capacity expansion
  if (db->count == db->capacity){
    size_t new_cap =
        db->capacity == 0 ? 8 : (size_t)(db->capacity * 1.61803398875);

    // Guard against truncation with small numbers
    if (new_cap <= db->capacity) {
      new_cap = db->capacity + 1;
    }

    // Reallocate the array buffer
    todo_t *new_items = realloc(db->items, new_cap * sizeof(*new_items));
    if (new_items == NULL) {
      perror("realloc");
      exit(EXIT_FAILURE);
    }

    db->items = new_items;
    db->capacity = new_cap;
  }
  // Store the item (shallow copy of struct) and advance count
  db->items[db->count++] = *item;
}

// remove path from end
// gives ownership of string pointer to caller via *item
int tdb_pop(todo_db *db, todo_t *item) {
  if (!db || db->count == 0 || !item) {
    return 0;
  }
  db->count--;
  *item = db->items[db->count];
  return 1;
}

// Return last path string without removing
int tdb_top(todo_db *db, todo_t *item) {
  if (!db || db->count == 0 || !item) {
    return 0;
  }

  *item = db->items[db->count - 1];
  return 1;
}

// Access path string at index
int tdb_get(const todo_db *db, size_t index, todo_t *item) {
  if (!db || index >= db->count) {
    return 0;
  }

  *item = db->items[index];
  return 1;
}

// Clean up allocated memory
void tdb_free(todo_db *db) {
    if (!db) return;

    for (size_t i = 0; i < db->count; i++) {
        free(db->items[i].title);
        free(db->items[i].description);
    }

    free(db->items);

    db->items = NULL;
    db->count = 0;
    db->capacity = 0;
}


////////////////////////////////
// Global helpers

// #1
// check if string exists in null-terminated array
static bool str_in_list(const char *str, const char *list[]) {
  for (size_t i = 0; list[i] != NULL; i++) {
    if (strcmp(str, list[i]) == 0) {
      return true;
    }
  }
  return false;
}

// #2
// implements policies for path traversal
bool accord(const char *path_name, mode_t mode) {
  // Directories to skip
  static const char *ignored_dirs[] = {
      ".git", ".vscode",     ".idea",  "node_modules", "target", "build",
      "dist", "__pycache__", ".cache", ".venv", NULL};

  // Rule for directories
  if (S_ISDIR(mode)) {
    if (str_in_list(path_name, ignored_dirs)) {
      return false;
    }
    // Allow all other directories so we can recurse into them
    return true;
  }

  // Rule for regular files
  if (S_ISREG(mode)) {
    // Exact filenames without standard extensions
    static const char *exact_files[] = {"Makefile", "CMakeLists.txt",
                                        "Dockerfile", ".gitignore", NULL};

    if (str_in_list(path_name, exact_files)) {
      return true;
    }

    // Extract file extension (.c, .rs, .py, etc.)
    const char *ext = strrchr(path_name, '.');
    if (!ext) {
      return false; // No extension and not an exact match
    }

    static const char *supported_exts[] = {
        ".c",    ".h",    ".cc",   ".cpp",  ".cxx",   ".hpp",  ".hh",
        ".rs",   ".go",   ".zig",  ".odin", ".py",    ".rb",   ".lua",
        ".sh",   ".bash", ".js",   ".mjs",  ".ts",    ".jsx",  ".tsx",
        ".html", ".css",  ".sql",  ".md",   ".toml",  ".yaml", ".yml",
        ".ini",  ".json", ".java", ".kt",   ".swift", NULL};

    return str_in_list(ext, supported_exts);
  }

  // Reject everything else (symlinks, sockets, devices, etc.)
  return false;
}

/*
 * FIXME:(#1) [RECURSION LIMITATION]
 *        Each recursion layer allocates `char path[PATH_MAX]` (4096 bytes) on
 * the stack, On very deep folder structures this risks stack overflow. Future
 * refactor: Flatten into an iterative loop using an explicit queue.
 */

/*
 * TODO:(#5) [COMMENT-STYLE MAPPING]
 *       Instead of re-inspecting file extensions downstream in `parse()`,
 *       map extensions to a comment-style enum during discovery in `accord()`:
 *           - STYLE_C_LIKE     (//, / * ... * /)  => .c, .rs, .go, .js, etc.
 *           - STYLE_HASH_LIKE  (#)              => .py, .sh, .rb, .yaml, etc.
 *           - STYLE_SQL_LIKE   (--)             => .sql, .lua, etc.
 *       => propagate this metadata alongside the path to avoid redundant
 * checks.
 */

/* NOTE:(#2) we will do batching. */

// #3
// Recursively iterate over the directory and collect matched files into 'db'.
void file_traverser(const char *dir_path, path_db *db) {
  DIR *dir = opendir(dir_path);
  if (dir == NULL) {
    perror(dir_path);
    return;
  }

  struct dirent *ent;
  struct stat states;

  while ((ent = readdir(dir)) != NULL) {
    // Skip "." and ".."
    if (ent->d_name[0] == '.' &&
        (ent->d_name[1] == '\0' ||
         (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
      continue;
    }

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);

    if (lstat(full_path, &states) != 0) {
      continue;
    }

    if (!accord(ent->d_name, states.st_mode)) {
      continue;
    }

    if (S_ISDIR(states.st_mode)) {
      // Pass the db pointer down to recursive calls
      file_traverser(full_path, db);
    } else {
      pdb_push(db, full_path);
    }
  }
  closedir(dir);
}

////////////////////////////////////////////////////////////////
// Reads target files into a null-terminated buffer for parsing.
// Also used to read policy files (e.g. .gitignore).
//
// NOTE:(#1) [MEMORY CONTRACT]
//       Caller owns the returned pointer and must release it with free().
//       Returns NULL on I/O or allocation failure.
//
// NOTE:(#2) [TODO SYNTAX SPEC]
//       <MARKER>[(<META>)]: <TITLE>
//       <EMPTY_COMMENT_LINE>
//       <BODY_LINE_1>
//       <BODY_LINE_2>
//       ...
//
//       - MARKER : TODO | FIXME | NOTE | HACK
//       - META   : Optional attributes (#id, p=priority, state=status)
//       - TITLE  : Single-line headline immediately following ": "
//       - GAP    : Blank comment line separating title from body
//       - BODY   : Multiline description; comment prefixes stripped
//
// EXAMPLE:
//       // TODO(#42, p=80, state=in_progress): Title of the task here
//       //
//       // Description starts after this empty comment line.
//       // It can span as many comment lines as needed.
char *parse(char *path) {
  // null checking
  if (path == NULL) {
    fprintf(stderr, "Error: Invalid path pointer.\n");
    return NULL;
  }

  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    perror("Error: Opening file.\n");
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    perror("Error: fseek.\n");
    fclose(file);
    return NULL;
  }

  long fsize = ftell(file);

  if (fsize < 0) {
    perror("Error: ftell.\n");
    fclose(file);
    return NULL;
  }

  rewind(file);

  char *buff = (char *)malloc(fsize + 1);
  if (buff == NULL) {
    fprintf(stderr, "Error: memory allocation failed.\n");
    fclose(file);
    return NULL;
  }

  size_t read_bytes = fread(buff, 1, fsize, file);

  if (read_bytes != (size_t)fsize) {
    if (ferror(file)) {
      perror("Error: fread.\n");
    }
    free(buff);
    fclose(file);
    return NULL;
  }

  buff[read_bytes] = '\0';
  fclose(file);
  return buff;
}

///////////////////////////////
// entry point
int main(void) {
  char cwd[PATH_MAX];

  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("getcwd");
    return 1;
  }

  path_db db = {0};
  log_info(LOG_ANSI_DIM "PDB Initialized" LOG_ANSI_RESET);

  file_traverser(cwd, &db);

  log_info(LOG_ANSI_DIM "Collected %zu files for parsing." LOG_ANSI_RESET,
           db.count);
  for (size_t i = 0; i < db.count; i++) {
    printf("[%zu]: %s\n", i + 1, db.item[i]);
  }

  printf("\n");
  log_info(LOG_ANSI_DIM "parser initialized..." LOG_ANSI_RESET);
  log_info(LOG_ANSI_DIM "parsing `db.item[3]`..." LOG_ANSI_RESET);
  char *tmp = parse(db.item[2]);
  printf("%s\n", tmp);

  free(tmp);
  pdb_free(&db);

  return 0;
}
