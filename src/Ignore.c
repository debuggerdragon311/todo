#include "./../include/Todo.h"

void gitignore_free(gitignore_t *gi) {
  if (!gi)
    return;
  for (size_t i = 0; i < gi->count; i++) {
    free(gi->patterns[i]);
  }
  free(gi->patterns);
  gi->patterns = NULL;
  gi->count = gi->capacity = 0;
}

// Load patterns from .gitignore file
bool gitignore_load(gitignore_t *gi, const char *root_dir) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/.gitignore", root_dir);

  FILE *f = fopen(path, "r");
  if (!f)
    return false;

  gi->patterns = NULL;
  gi->count = 0;
  gi->capacity = 0;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = '\0';

    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;

    if (*p == '\0' || *p == '#')
      continue;

    // Strip trailing slash (e.g., ".archives/" -> ".archives")
    size_t len = strlen(p);
    while (len > 0 &&
           (p[len - 1] == '/' || p[len - 1] == ' ' || p[len - 1] == '\t')) {
      p[--len] = '\0';
    }

    if (gi->count == gi->capacity) {
      gi->capacity = (gi->capacity == 0) ? 16 : gi->capacity * 2;
      gi->patterns = realloc(gi->patterns, gi->capacity * sizeof(char *));
    }
    gi->patterns[gi->count++] = strdup(p);
  }

  fclose(f);
  return true;
}

// Check if relative path matches any pattern
bool gitignore_matches(const gitignore_t *gi, const char *rel_path) {
  if (!gi || gi->count == 0 || !rel_path)
    return false;

  // Extract basename (e.g. "src/main.c" -> "main.c")
  const char *basename = strrchr(rel_path, '/');
  basename = basename ? basename + 1 : rel_path;

  for (size_t i = 0; i < gi->count; i++) {
    const char *pat = gi->patterns[i];
    size_t pat_len = strlen(pat);

    // 1. Direct match on relative path or filename (e.g. ".clangd", "todo",
    // "*.log")
    if (fnmatch(pat, rel_path, 0) == 0)
      return true;
    if (fnmatch(pat, basename, 0) == 0)
      return true;

    // 2. Directory prefix: rel_path starts with "pat/"
    // e.g. pat=".archives", rel_path=".archives/sqlite/fts5_index.c"
    if (strncmp(rel_path, pat, pat_len) == 0 &&
        (rel_path[pat_len] == '/' || rel_path[pat_len] == '\0')) {
      return true;
    }

    // 3. Subdirectory match anywhere in path: "/pat/" or ending in "/pat"
    // e.g. pat="build", rel_path="core/build/cache.o"
    char needle[PATH_MAX];
    snprintf(needle, sizeof(needle), "/%s/", pat);
    if (strstr(rel_path, needle) != NULL)
      return true;

    snprintf(needle, sizeof(needle), "/%s", pat);
    size_t nlen = strlen(needle);
    size_t rlen = strlen(rel_path);
    if (rlen >= nlen && strcmp(rel_path + rlen - nlen, needle) == 0) {
      return true;
    }
  }

  return false;
}

// Filter the path_db in-place against loaded gitignore rules
void pdb_filter_gitignore(path_db *db, const gitignore_t *gi,
                          const char *root_dir) {
  if (!gi || gi->count == 0)
    return;

  size_t root_len = strlen(root_dir);
  size_t write_idx = 0;

  for (size_t read_idx = 0; read_idx < db->count; read_idx++) {
    char *full_path = db->item[read_idx];

    // Compute relative path
    const char *rel_path = full_path;
    if (strncmp(full_path, root_dir, root_len) == 0) {
      rel_path = full_path + root_len;
      if (*rel_path == '/')
        rel_path++;
    }

    if (gitignore_matches(gi, rel_path)) {
      free(full_path);
    } else {
      db->item[write_idx++] = full_path;
    }
  }

  db->count = write_idx;
}
