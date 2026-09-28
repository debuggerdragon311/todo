// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

#include "./../include/Todo.h"
#include "./../include/server.h"
#include "./../include/log.h"

#define LOG_IMPLEMENTATION
#include "log.h"

int main(void) {
  char cwd[PATH_MAX];

  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("getcwd");
    return 1;
  }

  // Load .gitignore
  gitignore_t gi = {0};
  if (gitignore_load(&gi, cwd)) {
    log_info(LOG_ANSI_DIM
             "Loaded %zu pattern(s) from .gitignore." LOG_ANSI_RESET,
             gi.count);
  } else {
    log_info(LOG_ANSI_DIM "No .gitignore found; proceeding with default "
                          "traversal." LOG_ANSI_RESET);
  }

  path_db db = {0};
  file_traverser(cwd, &gi, cwd, &db);
  log_info(LOG_ANSI_DIM "Collected %zu files for parsing." LOG_ANSI_RESET,
           db.count);

  gitignore_free(&gi);

  // Parse tasks from all files
  todo_db todos = {0};
  log_info(LOG_ANSI_DIM "Extracting tasks from source files..." LOG_ANSI_RESET);

  for (size_t i = 0; i < db.count; i++) {
    char *content = read_file(db.item[i]);
    if (content) {
      parse_todos_from_buffer(db.item[i], content, &todos);
      free(content);
    }
  }

  log_success("Extracted %zu task(s) from %zu source files.", todos.count,
              db.count);

  // Launch HTTP server daemon
  const uint16_t port = 8080;
  log_info("Dashboard available at: " LOG_ANSI_BOLD_CYAN
           "http://localhost:%u" LOG_ANSI_RESET,
           port);
  server_start(port, &todos);

  tdb_free(&todos);
  pdb_free(&db);

  return 0;
}
