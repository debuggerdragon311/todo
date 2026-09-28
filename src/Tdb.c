#include "./../include/Todo.h"

void tdb_push(todo_db *db, const todo_t *item) {
  if (!db || !item) {
    return;
  }

  // capacity expansion
  if (db->count == db->capacity) {
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

int tdb_pop(todo_db *db, todo_t *item) {
  if (!db || db->count == 0 || !item) {
    return 0;
  }
  db->count--;
  *item = db->items[db->count];
  return 1;
}

int tdb_top(todo_db *db, todo_t *item) {
  if (!db || db->count == 0 || !item) {
    return 0;
  }

  *item = db->items[db->count - 1];
  return 1;
}

int tdb_get(const todo_db *db, size_t index, todo_t *item) {
  if (!db || index >= db->count) {
    return 0;
  }

  *item = db->items[index];
  return 1;
}

void tdb_free(todo_db *db) {
  if (!db)
    return;

  for (size_t i = 0; i < db->count; i++) {
    free(db->items[i].title);
    free(db->items[i].description);
  }

  free(db->items);

  db->items = NULL;
  db->count = 0;
  db->capacity = 0;
}