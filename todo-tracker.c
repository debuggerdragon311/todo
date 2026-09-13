// SPDX-License-Identifier: AGPL-3.0-only 
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>


/*
////////////////////////////////
// ROADMAP (will be removed soon!)
  [x] Phase 0: Dynamic Array (DArray) foundation for paths and todos.
  [ ] Phase 1: Robust Path Discovery & Traversal
               - Replace stat() with lstat() (avoid symlink cycles).
               - Move hardcoded ignores into a clean pattern matcher.
               - Prevent stack exhaustion on deep recursion.
  [ ] Phase 2: Memory Model Hardening (Arena / Pool)
               - Stop raw strdup/free fragmentation.
               - Linear allocation for char *s (critical for high file counts).
  [ ] Phase 3: Zero-Copy File Ingestion (mmap)
               - Map files to memory pages rather than reading byte-by-byte.
  [ ] Phase 4: Lexer & Comment Parser (State Machine)
               - Detect comments: //, / *... * /, #, --
               - Ignore false positives inside char * literals: "TODO: fix"
               - Strip comment prefixes and normalize whitespace.
  [ ] Phase 5: Query & Output Engine
               - Format output (Terminal, Markdown, JSON).
               - Filter/sort by priority, file type, line number.
  [ ] Phase 6: Multi-Threading (Google-Scale Performance)
               - Multi-threaded worker queue for reading and lexing.

////////////////////////////////
//GOALS
 1.Filtering — Respect .gitignore and skip build artifacts dynamically so you only
             touch relevant source code.

 2.Safety — Detect binary files and guard against symlink loops so the tool never crashes or
            hangs.

 3.Parsing — Use a state machine to extract real comments across languages while
           ignoring code and char * literals.

 4.Output — Emit clean .txt, JSON, and proper exit codes for developers and
          automated CI/CD pipelines.
 

////////////////////////////////
// ARCHITECTURE PIPELINE (will be removed soon!)
          cwd
           │
           ▼
   ┌───────────────┐
   │ file_traverser│ ── (Directory walk via lstat)
   └───────┬───────┘
           │ filter: skip .git, binary extensions (.png, .so)
           ▼
     [ path_db ] ────► Dynamic Array of valid source paths
           │
           │ (Pipeline Stage 2: Ingestion & Parsing)
           ▼
   ┌───────────────┐
   │  file_reader  │ ── (mmap file content directly to memory)
   └───────┬───────┘
           │
           ▼
   ┌───────────────┐
   │  todo_lexer   │ ── (FSM: extracts text inside comments only)
   └───────┬───────┘
           │
           ▼
     [ todo_db ] ────► Dynamic Array of clean, parsed TODO records
           │
           ▼
   ┌───────────────┐
   │  report_view  │ ── (Format & dump to terminal / stdout)
   └───────────────┘
*/

#include <stdio.h>     // io
#include <stdlib.h>    // size_t
#include <sys/types.h> // file access
#include <dirent.h>    // dir. fns.
#include <string.h>    // string ops.
#include <sys/stat.h>  // file stats
#include <unistd.h>    // getcwd()
#include <limits.h>    // PATH_MAX 4096
// experimental addition (will be removed soon!)
#include <fnmatch.h>   // fnmatch()

/*
 * TODO: Replace this macro with C99 compound zero initializer: (todo){0}
 *       Empty root init. obj.
 */
#define empty {0,0,"",{"","",0,0,0},""}

////////////////////////////////
// path (addr.) object
typedef struct Path {
  char * file_name;
  char * file_path;
  int line_number;
  int column_number;
  /*
   * TODO: Add file-type enumeration for language-aware parsing:
   *       FT_C_LIKE (//, / *...* /), FT_SHELL_LIKE (#), FT_SQL_LIKE (--)
   *       => add extra fields
   *           - file types
   *           - lang. awareness
   *           - ...
   */
  int optional;
} path;

////////////////////////////////
// todo object -> tasks (root)
typedef struct Todo {
  int ref_id;
  int priority; // max is <100
  char * state; // closed, open, in_progress
  path address; // exact location of the task
  char * data;  // meta-data about the task
} todo;

////////////////////////////////
// DArray for paths obj.
typedef struct Storage {
  path *item;
  size_t count;
  size_t capacity;
} path_db;

// add path to the end
void pdb_push(path_db *db, path item) {
  // cap. expansion
  if (db->count == db->capacity){
    size_t new_cap = db->capacity == 0 ? 8 : db->capacity * 2;

    /*
     * TODO: Check for size_t overflow before multiplication if scaling huge
     */
    path *new_item = realloc(
      db->item,
      new_cap *sizeof(*new_item)
    );

    if (new_item == NULL) {
      perror("realloc");
      exit(EXIT_FAILURE);
    }
    // copy paths object in array
    db->item = new_item;
    db->capacity = new_cap;
  }

  /*
   * BUG (WARNING):
   * 'item.file_name' and 'item.file_path' must be OWNED heaps (e.g. strdup).
   * If you pass temporary stack buffers (like `char path[PATH_MAX]` from traverser),
   * they will dangle or corrupt once the stack frame collapses!
   */
  // increase counter
  db->item[db->count++] = item;
};

// remove path from end
int pdb_pop(path_db *db, path *item) {
  if (db->count == 0) {
    return 0;
  }
  *item = db->item[--db->count];
  return 1;
};

// return last path without removing
path *pdb_top(path_db *db) {
  if (db->count == 0) {
    return NULL;
  }
  return &db->item[db->count-1];
};

/*
 * TODO: traverse the item from index
 * Return pointer to item for in-place inspection/mutation (avoids copying structs)
 */
path *pdb_get(const path_db *db, size_t index) {
  if (index >= db->count) {
    return NULL;
  }
  return &db->item[index];
}

// Clean up allocated memory
void pdb_free(path_db *db) {
  if (!db) return;

  /*
   * NOTE: Only call free() here if paths were duplicated with malloc/strdup.
   */
  for (size_t i = 0; i < db->count; i++) {
    free(db->item[i].file_name);
    free(db->item[i].file_path);
  }
  free(db->item);
  db->item = NULL;
  db->count = 0;
  db->capacity = 0;
}

////////////////////////////////
// Global helpers

/*
 * FIXME: [RECURSION LIMITATION]
 *        Each recursion layer allocates `char path[PATH_MAX]` (4096 bytes) on the stack.
 *        On very deep folder structures this risks stack overflow.
 *        Future refactor: Flatten into an iterative loop using an explicit queue.
 */
void file_traverser(char * dir_path) {
  // support for sorting by
  // - a-z
  // - timestamp
  DIR *dir;
  struct dirent *ent;
  struct stat states;

  dir = opendir(dir_path);
  if (dir == NULL) {
    perror(dir_path);
    return;
  }

  // if a directory entry is a file or directory
  while ((ent = readdir(dir)) != NULL) {

    /*
     * FIXME: temporary policies for traversal
     * [think about .gitignore file can be used]
     * make it a contained fn. to call here for diverse use.
     * (thinking about regex support...)
     *     - special hidden files .<file>
     *     - other format files no need (svg, png, pdf...)
     */
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    // experimental addition
    if (fnmatch("*git*", ent->d_name, 0) == 0) {
      continue;
    }
    if (strcmp(ent->d_name, ".tmp") == 0) {
      continue;
    }
    if (strcmp(ent->d_name, ".vscode") == 0) {
      continue;
    }
    // temporary specific folders of mine
    if (strcmp(ent->d_name, ".c_guide") == 0 || strcmp(ent->d_name, "tatr") == 0) {
      continue;
    }
    // Skip symbolic links entirely to guarantee cycle freedom
    if (S_ISLNK(states.st_mode)) {
      continue;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);

    if (lstat(path, &states) == 0) {
      if (S_ISDIR(states.st_mode)) {
        file_traverser(path);
      } else {
        // ...
        printf("%s\n", path); // temporary print for debuging
      }
    }
  }
  closedir(dir);
}

int main(void) {
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("getcwd");
    return 1;
  }
  file_traverser(cwd);
  return 0;
}
