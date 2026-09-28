#include "./../include/Todo.h"

static bool str_in_list(const char *str, const char *list[]) {
  for (size_t i = 0; list[i] != NULL; i++) {
    if (strcmp(str, list[i]) == 0) {
      return true;
    }
  }
  return false;
}

bool accord(const char *path_name, mode_t mode) {
  // Directories to skip
  static const char *ignored_dirs[] = {
      ".git", ".vscode",     ".idea",  "node_modules", "target", "build",
      "dist", "__pycache__", ".cache", ".venv",        NULL};

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

/* NOTE:(#2) we will do batching. */

void file_traverser(const char *dir_path, const gitignore_t *gi,
                    const char *root_dir, path_db *db) {
  DIR *dir = opendir(dir_path);
  if (dir == NULL) {
    perror(dir_path);
    return;
  }

  struct dirent *ent;
  struct stat states;

  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.' &&
        (ent->d_name[1] == '\0' ||
         (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
      continue;
    }

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);

    if (lstat(full_path, &states) != 0)
      continue;
    if (!accord(ent->d_name, states.st_mode))
      continue;

    // Check gitignore before descending or adding
    if (gi && root_dir) {
      const char *rel = full_path + strlen(root_dir);
      if (*rel == '/')
        rel++;
      if (gitignore_matches(gi, rel)) {
        continue; // Skip .archives/ completely without entering!
      }
    }

    if (S_ISDIR(states.st_mode)) {
      file_traverser(full_path, gi, root_dir, db);
    } else {
      pdb_push(db, full_path);
    }
  }
  closedir(dir);
}
