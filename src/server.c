// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

#include "./../include/server.h"
#include "./../include/log.h"
#include "./../include/assets.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <sys/socket.h>

static const char *get_filename(const char *path) {
  if (!path)
    return "unknown";
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static void json_append_escaped(char **buf, size_t *cap, size_t *len,
                                const char *str) {
  if (!str)
    return;

  for (const char *p = str; *p; p++) {
    const char *sub = NULL;
    char tmp[2] = {*p, '\0'};

    switch (*p) {
    case '"':
      sub = "\\\"";
      break;
    case '\\':
      sub = "\\\\";
      break;
    case '\b':
      sub = "\\b";
      break;
    case '\f':
      sub = "\\f";
      break;
    case '\n':
      sub = "\\n";
      break;
    case '\r':
      sub = "\\r";
      break;
    case '\t':
      sub = "\\t";
      break;
    default:
      sub = tmp;
      break;
    }

    size_t sublen = strlen(sub);
    while (*len + sublen + 1 >= *cap) {
      *cap = (*cap == 0) ? 4096 : (*cap * 2);
      *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, sub, sublen);
    *len += sublen;
  }
  (*buf)[*len] = '\0';
}

static void json_append_raw(char **buf, size_t *cap, size_t *len,
                            const char *raw) {
  if (!raw)
    return;
  size_t rlen = strlen(raw);
  while (*len + rlen + 1 >= *cap) {
    *cap = (*cap == 0) ? 4096 : (*cap * 2);
    *buf = realloc(*buf, *cap);
  }
  memcpy(*buf + *len, raw, rlen);
  *len += rlen;
  (*buf)[*len] = '\0';
}

static char *todos_to_json(const todo_db *todos) {
  size_t cap = 8192;
  size_t len = 0;
  char *buf = malloc(cap);
  buf[0] = '\0';

  json_append_raw(&buf, &cap, &len, "[\n");

  for (size_t i = 0; i < todos->count; i++) {
    const todo_t *t = &todos->items[i];

    const char *state_str = "open";
    if (t->state == TODO_STATE_IN_PROGRESS)
      state_str = "in_progress";
    else if (t->state == TODO_STATE_DONE)
      state_str = "closed";
    else if (t->state == TODO_STATE_BLOCKED)
      state_str = "closed";

    const char *type_str = "TODO";
    if (t->kind == TODO_KIND_FIXME)
      type_str = "FIXME";
    else if (t->kind == TODO_KIND_NOTE)
      type_str = "NOTE";
    else if (t->kind == TODO_KIND_HACK)
      type_str = "HACK";

    int64_t ref_id = (t->ref_id >= 0) ? t->ref_id : (int64_t)(i + 1);

    char num_buf[256];
    snprintf(num_buf, sizeof(num_buf),
             "  {\n"
             "    \"ref_id\": %ld,\n"
             "    \"priority\": %u,\n"
             "    \"state\": \"%s\",\n"
             "    \"type\": \"%s\",\n"
             "    \"data\": \"",
             ref_id, t->priority, state_str, type_str);
    json_append_raw(&buf, &cap, &len, num_buf);

    json_append_escaped(&buf, &cap, &len, t->title ? t->title : "");
    json_append_raw(&buf, &cap, &len, "\",\n");

    if (t->description) {
      json_append_raw(&buf, &cap, &len, "    \"description\": \"");
      json_append_escaped(&buf, &cap, &len, t->description);
      json_append_raw(&buf, &cap, &len, "\",\n");
    }

    const char *fname = get_filename(t->loc.file_path);
    snprintf(num_buf, sizeof(num_buf),
             "    \"address\": {\n"
             "      \"file_name\": \"%s\",\n"
             "      \"file_path\": \"",
             fname);
    json_append_raw(&buf, &cap, &len, num_buf);
    json_append_escaped(&buf, &cap, &len,
                        t->loc.file_path ? t->loc.file_path : "");

    snprintf(num_buf, sizeof(num_buf),
             "\",\n"
             "      \"line_number\": %u,\n"
             "      \"column_number\": %u\n"
             "    }\n"
             "  }%s\n",
             t->loc.line_number, t->loc.column_number,
             (i + 1 < todos->count) ? "," : "");
    json_append_raw(&buf, &cap, &len, num_buf);
  }

  json_append_raw(&buf, &cap, &len, "]\n");
  return buf;
}

static char *html_inject(const char *template, const char *placeholder,
                         const char *replacement) {
  if (!template || !placeholder || !replacement)
    return NULL;

  const char *pos = strstr(template, placeholder);
  if (!pos)
    return strdup(template);

  size_t prefix_len = pos - template;
  size_t placeholder_len = strlen(placeholder);
  size_t rep_len = strlen(replacement);
  size_t suffix_len = strlen(pos + placeholder_len);

  size_t total_size = prefix_len + rep_len + suffix_len + 1;
  char *output = malloc(total_size);
  if (!output)
    return NULL;

  memcpy(output, template, prefix_len);
  memcpy(output + prefix_len, replacement, rep_len);
  memcpy(output + prefix_len + rep_len, pos + placeholder_len, suffix_len);
  output[total_size - 1] = '\0';

  return output;
}

static void send_response(int client_fd, const char *status,
                          const char *content_type, const char *body,
                          size_t body_len) {
  char header[512];
  int hlen = snprintf(header, sizeof(header),
                      "HTTP/1.1 %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Connection: close\r\n\r\n",
                      status, content_type, body_len);

  send(client_fd, header, (size_t)hlen, 0);
  if (body && body_len > 0) {
    send(client_fd, body, body_len, 0);
  }
}

static void url_decode(char *dst, const char *src) {
  char a, b;
  while (*src) {
    if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
        (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a')
        a -= 'a' - 'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a')
        b -= 'a' - 'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';
      *dst++ = 16 * a + b;
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static void open_file_in_editor(const char *file, int line, int col) {
  const char *editor = getenv("VISUAL");
  if (!editor || strlen(editor) == 0)
    editor = getenv("EDITOR");

  char cmd[PATH_MAX * 2];

  if (editor && strlen(editor) > 0) {
    if (strstr(editor, "code") || strstr(editor, "cursor")) {
      snprintf(cmd, sizeof(cmd), "%s -g \"%s\":%d:%d >/dev/null 2>&1 &", editor,
               file, line, col);
    } else if (strstr(editor, "subl")) {
      snprintf(cmd, sizeof(cmd), "%s \"%s\":%d:%d >/dev/null 2>&1 &", editor,
               file, line, col);
    } else {
      snprintf(cmd, sizeof(cmd), "%s +%d \"%s\" >/dev/null 2>&1 &", editor,
               line, file);
    }
  } else {
#if defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), "open \"%s\" >/dev/null 2>&1 &", file);
#elif defined(_WIN32)
    snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", file);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", file);
#endif
  }

  log_info("Executing editor command: %s", cmd);
  system(cmd);
}

void server_start(uint16_t port, const todo_db *todos) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr.s_addr = INADDR_ANY,
  };

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(server_fd);
    return;
  }

  if (listen(server_fd, 32) < 0) {
    perror("listen");
    close(server_fd);
    return;
  }

  log_info(LOG_ANSI_BOLD_GREEN
           "Daemon listening on http://localhost:%u" LOG_ANSI_RESET,
           port);

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0)
      continue;

    char request_buf[2048] = {0};
    ssize_t bytes_read =
        recv(client_fd, request_buf, sizeof(request_buf) - 1, 0);

    if (bytes_read > 0) {
      char method[16] = {0};
      char path[256] = {0};
      sscanf(request_buf, "%15s %255s", method, path);

      // Route 1: API Endpoint
      if (strcmp(path, "/api/todos") == 0) {
        char *json = todos_to_json(todos);
        send_response(client_fd, "200 OK", "application/json", json,
                      strlen(json));
        free(json);
      }
      // Route 2: CSS (Zero-copy in-memory)
      else if (strcmp(path, "/style.css") == 0) {
        send_response(client_fd, "200 OK", "text/css; charset=utf-8",
                      asset_style_css, asset_style_css_len);
      }
      // Route 3: JavaScript (Zero-copy in-memory)
      else if (strcmp(path, "/script.js") == 0) {
        send_response(client_fd, "200 OK",
                      "application/javascript; charset=utf-8", asset_script_js,
                      asset_script_js_len);
      }
      // Route 4: Favicon (Zero-copy in-memory)
      else if (strcmp(path, "/favicon.ico") == 0) {
        send_response(client_fd, "200 OK", "image/x-icon",
                      (const char *)asset_favicon, asset_favicon_len);
      }
      // Route 5: Icon / Logo (Zero-copy in-memory)
      else if (strcmp(path, "/icon.png") == 0) {
        send_response(client_fd, "200 OK", "image/png",
                      (const char *)asset_icon, asset_icon_len);
      }
      // Route 6: Dashboard HTML (Inject JSON into in-memory template)
      else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        char *json = todos_to_json(todos);
        char *final_html = html_inject(asset_index_html,
                                       "/* __INJECTED_TODOS__ */ null", json);

        if (final_html) {
          send_response(client_fd, "200 OK", "text/html; charset=utf-8",
                        final_html, strlen(final_html));
          free(final_html);
        } else {
          send_response(client_fd, "200 OK", "text/html; charset=utf-8",
                        asset_index_html, asset_index_html_len);
        }
        free(json);
      }
      // Route 7: Open File in System Editor
      else if (strncmp(path, "/api/open", 9) == 0) {
        char file[PATH_MAX] = {0};
        int line = 1, col = 1;

        const char *q = strchr(path, '?');
        if (q) {
          char query[PATH_MAX * 2];
          strncpy(query, q + 1, sizeof(query) - 1);

          char *token = strtok(query, "&");
          while (token) {
            if (strncmp(token, "file=", 5) == 0) {
              url_decode(file, token + 5);
            } else if (strncmp(token, "line=", 5) == 0) {
              line = atoi(token + 5);
            } else if (strncmp(token, "col=", 4) == 0) {
              col = atoi(token + 4);
            }
            token = strtok(NULL, "&");
          }
        }

        if (strlen(file) > 0) {
          open_file_in_editor(file, line, col);
          send_response(client_fd, "200 OK", "text/plain", "OK", 2);
        } else {
          send_response(client_fd, "400 Bad Request", "text/plain",
                        "Missing file param", 18);
        }
      } else {
        send_response(client_fd, "404 Not Found", "text/plain", "404 Not Found",
                      13);
      }
    }

    close(client_fd);
  }

  close(server_fd);
}
