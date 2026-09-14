// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

#include <stdio.h>     // io
#include <stdbool.h>   // bool
#include <stdlib.h>    // size_t
#include <sys/types.h> // file access
#include <dirent.h>    // dir. fns.
#include <string.h>    // string ops.
#include <sys/stat.h>  // file stats
#include <unistd.h>    // getcwd()
#include <limits.h>    // PATH_MAX 4096
#include <fnmatch.h>   // fnmatch()

#define LOG_IMPLEMENTATION
#include "log.h"

////////////////////////////////
// meta (location) object
typedef struct Location {
  char* file_name;
  char* file_path;
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
} location;

////////////////////////////////
// todo object -> tasks (root)
typedef struct Todo {
  int ref_id;
  int priority; // max is <100
  char* state; // closed, open, in_progress
  location loc; // exact location of the task
  char* data;  // meta-data about the task
} todo;

////////////////////////////////
// DArray for paths obj.
typedef struct Storage {
  char* *item;
  size_t count;
  size_t capacity;
} path_db;

// Add path to the end (duplicates string so stack buffers don't dangle)
void pdb_push(path_db *db, const char *item) {
  if (!db || !item) return;

  // Capacity expansion
  if (db->count == db->capacity) {
    size_t new_cap = db->capacity == 0 ? 8 : db->capacity * 2;

    char **new_item = realloc(
      db->item,
      new_cap * sizeof(*new_item)
    );

    if (new_item == NULL) {
      perror("realloc");
      exit(EXIT_FAILURE);
    }

    db->item = new_item;
    db->capacity = new_cap;
  }

  // Store heap-allocated copy so stack buffers (e.g., path[PATH_MAX]) stay valid
  db->item[db->count++] = strdup(item);
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
    free(db->item[db->count]); // Prevent leak if caller ignores the popped value
  }
  return 1;
}

// Return last path string without removing
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
  if (!db) return;

  for (size_t i = 0; i < db->count; i++) {
    free(db->item[i]);
  }
  free(db->item);
  db->item = NULL;
  db->count = 0;
  db->capacity = 0;
}

////////////////////////////////
// Global helpers

// #1
// implements policies for path traversal
bool accord(const char *path_name, mode_t mode) {
  // Rule for directories
  if (S_ISDIR(mode)) {
    // Directories to ignore
    if (strcmp(path_name, ".git") == 0 || strcmp(path_name, ".vscode") == 0) {
      return false;
    }
    // Allow all other directories so we can recurse into them (e.g. "src")
    return true;
  }

  // Rule for regular files
  if (S_ISREG(mode)) {
    return (fnmatch("*.c",   path_name, 0) == 0
    || fnmatch("*.h",   path_name, 0) == 0
    || fnmatch("*.cc",  path_name, 0) == 0
    || fnmatch("*.cpp", path_name, 0) == 0
    || fnmatch("*.hpp", path_name, 0) == 0);
  }

  // Reject everything else (symlinks, sockets, devices, etc.)
  return false;
}

/*
 * FIXME: [RECURSION LIMITATION]
 *        Each recursion layer allocates `char path[PATH_MAX]` (4096 bytes) on the stack.
 *        On very deep folder structures this risks stack overflow.
 *        Future refactor: Flatten into an iterative loop using an explicit queue.
 */
// #2
// recursively iterate over the directory path
/* NOTE: we will do batching */
// Recursively iterate over the directory and collect matched files into 'db'
void file_traverser(const char* dir_path, path_db *db) {
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
      (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
      continue;
      }

    // Renamed to 'full_path' to avoid variable shadowing
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

///////////////////////////////
// entry point
int main(void) {
  char cwd[PATH_MAX];

  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("getcwd");
    return 1;
  }

  path_db db = {0};
  log_info(ANSI_DIM "PDB Initialized" ANSI_RESET);

  file_traverser(cwd, &db);

  log_info("Collected %zu files for parsing.", db.count);
  for (size_t i = 0; i < db.count; i++) {
    log_info("[%zu]: %s", i, db.item[i]);
  }

  pdb_free(&db);
  return 0;
}
