#ifndef TODO_H
#define TODO_H

#include <ctype.h>
#include <dirent.h>  // dir. fns.
#include <fnmatch.h> // fnmatch()
#include <limits.h>  // PATH_MAX 4096
#include <stdbool.h> // bool
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>     // io
#include <stdlib.h>    // size_t
#include <string.h>    // string ops.
#include <sys/stat.h>  // file stats
#include <sys/types.h> // file access
#include <unistd.h>    // getcwd()

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

typedef struct Todo {
  int64_t ref_id;     // -1 if omitted (triggers auto-generation)
  uint8_t priority;   // 0 - 100 (e.g. 50 default)
  todo_state_t state; // Enum instead of char*
  todo_kind_t kind;   // TODO vs FIXME vs NOTE
  location_t loc;     // File origin

  char *title;       // Heap-allocated single line
  char *description; // Heap-allocated multiline block (or NULL if omitted)
} todo_t;

typedef struct Storage {
  char **item;
  size_t count;
  size_t capacity;
} path_db;

typedef struct TodoDB {
  todo_t *items;
  size_t count;
  size_t capacity;
} todo_db;

typedef struct {
  char **patterns;
  size_t count;
  size_t capacity;
} gitignore_t;

// (Path Storage)
void pdb_push(path_db *db, const char *item);
int pdb_pop(path_db *db, char **item);
char *pdb_top(const path_db *db);
char *pdb_get(const path_db *db, size_t index);
void pdb_free(path_db *db);

// (Todo Storage)
void tdb_push(todo_db *db, const todo_t *item);
int tdb_pop(todo_db *db, todo_t *item);
int tdb_top(todo_db *db, todo_t *item);
int tdb_get(const todo_db *db, size_t index, todo_t *item);
void tdb_free(todo_db *db);

// (File I/O)
char *read_file(const char *path);

// (Directory Traversal)
bool accord(const char *path_name, mode_t mode);
void file_traverser(const char *dir_path, const gitignore_t *gi,
                    const char *root_dir, path_db *db);

// (Gitignore)
void gitignore_free(gitignore_t *gi);
bool gitignore_load(gitignore_t *gi, const char *root_dir);
bool gitignore_matches(const gitignore_t *gi, const char *rel_path);
void pdb_filter_gitignore(path_db *db, const gitignore_t *gi,
                          const char *root_dir);

// (Parser)
void parse_todos_from_buffer(const char *filepath, const char *content,
                             todo_db *todos);

#endif
