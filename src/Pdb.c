#include "./../include/Todo.h"

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

char *pdb_top(const path_db *db) {
  if (!db || db->count == 0) {
    return NULL;
  }
  return db->item[db->count - 1];
}

char *pdb_get(const path_db *db, size_t index) {
  if (!db || index >= db->count) {
    return NULL;
  }
  return db->item[index];
}

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