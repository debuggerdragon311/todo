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


#include "./../include/Todo.h"
#include "./../include/log.h"

// Strips comment markers. Returns NULL if the line is NOT a comment.
static const char *strip_comment_prefix(const char *line) {
  while (*line == ' ' || *line == '\t')
    line++;

  if (strncmp(line, "//", 2) == 0)
    line += 2;
  else if (strncmp(line, "/*", 2) == 0)
    line += 2;
  else if (strncmp(line, "--", 2) == 0)
    line += 2;
  else if (*line == '#' || *line == '*')
    line += 1;
  else
    return NULL;

  while (*line == ' ' || *line == '\t')
    line++;
  return line;
}

// Check if a line is a blank/empty comment line (e.g. "//" or " *")
static bool is_empty_comment(const char *line) {
  const char *s = strip_comment_prefix(line);
  if (!s)
    return false;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    s++;
  return (*s == '\0');
}

// for multi-line comments like `*/` -> `\0`
static void trim_trailing(char *str) {
  if (!str)
    return;
  size_t len = strlen(str);
  while (len > 0) {
    char c = str[len - 1];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      str[--len] = '\0';
    } else if (len >= 2 && str[len - 2] == '*' && str[len - 1] == '/') {
      len -= 2;
      str[len] = '\0';
    } else {
      break;
    }
  }
}

// Parse metadata inside the parentheses: e.g. "#42, p=80, state=in_progress"
static void parse_metadata(const char *meta_str, int64_t *ref_id,
                           uint8_t *priority, todo_state_t *state) {
  char buf[128];
  strncpy(buf, meta_str, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *tok = strtok(buf, ",");
  while (tok) {
    while (*tok == ' ' || *tok == '\t')
      tok++;

    if (*tok == '#') {
      *ref_id = atoll(tok + 1);
    } else if (strncmp(tok, "p=", 2) == 0) {
      int p = atoi(tok + 2);
      if (p >= 0 && p <= 100) {
        *priority = (uint8_t)p;
      }
    } else if (strncmp(tok, "state=", 6) == 0) {
      const char *val = tok + 6;
      if (strcmp(val, "open") == 0)
        *state = TODO_STATE_OPEN;
      else if (strcmp(val, "in_progress") == 0)
        *state = TODO_STATE_IN_PROGRESS;
      else if (strcmp(val, "blocked") == 0)
        *state = TODO_STATE_BLOCKED;
      else if (strcmp(val, "done") == 0)
        *state = TODO_STATE_DONE;
    }

    tok = strtok(NULL, ",");
  }
}

static bool parse_headline(const char *line, todo_t *out_item) {
  const char *comment = strip_comment_prefix(line);
  if (!comment)
    return false;

  static const char *markers[] = {"TODO", "FIXME", "NOTE", "HACK"};
  static const todo_kind_t kinds[] = {TODO_KIND_TODO, TODO_KIND_FIXME,
                                      TODO_KIND_NOTE, TODO_KIND_HACK};

  for (int k = 0; k < 4; k++) {
    size_t mlen = strlen(markers[k]);
    if (strncmp(comment, markers[k], mlen) == 0) {
      const char *cur = comment + mlen;

      while (*cur == ' ' || *cur == '\t')
        cur++;
      if (*cur != '(' && *cur != ':')
        return false;

      out_item->kind = kinds[k];
      out_item->ref_id = -1;
      out_item->priority = 50;
      out_item->state = TODO_STATE_OPEN;
      out_item->title = NULL;
      out_item->description = NULL;

      if (*cur == '(') {
        const char *close_paren = strchr(cur, ')');
        if (!close_paren)
          return false;

        char meta_buf[128] = {0};
        size_t meta_len = close_paren - (cur + 1);
        if (meta_len < sizeof(meta_buf)) {
          strncpy(meta_buf, cur + 1, meta_len);
          meta_buf[meta_len] = '\0';
          parse_metadata(meta_buf, &out_item->ref_id, &out_item->priority,
                         &out_item->state);
        }
        cur = close_paren + 1;
        while (*cur == ' ' || *cur == '\t')
          cur++;
      }

      if (*cur != ':')
        return false;
      cur++;
      while (*cur == ' ' || *cur == '\t')
        cur++;

      out_item->title = strdup(cur);
      trim_trailing(out_item->title);
      return true;
    }
  }
  return false;
}

// Main entry point
void parse_todos_from_buffer(const char *filepath, const char *content,
                             todo_db *todos) {
  if (!filepath || !content || !todos)
    return;

  // 1. Split file content into array of lines
  char **lines = NULL;
  size_t line_count = 0, line_cap = 0;

  const char *ptr = content;
  while (*ptr) {
    const char *start = ptr;
    while (*ptr && *ptr != '\n')
      ptr++;
    size_t len = ptr - start;
    if (len > 0 && start[len - 1] == '\r')
      len--;

    if (line_count == line_cap) {
      line_cap = line_cap == 0 ? 128 : line_cap * 2;
      lines = realloc(lines, line_cap * sizeof(char *));
    }
    lines[line_count] = malloc(len + 1);
    memcpy(lines[line_count], start, len);
    lines[line_count][len] = '\0';
    line_count++;

    if (*ptr == '\n')
      ptr++;
  }

  // 2. Scan lines
  for (size_t i = 0; i < line_count; i++) {
    todo_t item = {0};
    if (parse_headline(lines[i], &item)) {
      item.loc.file_path = filepath;
      item.loc.line_number = (uint32_t)(i + 1);

      // Check if followed by GAP (empty comment line)
      if (i + 1 < line_count && is_empty_comment(lines[i + 1])) {
        size_t j = i + 2;
        char desc_buf[4096] = {0};
        size_t desc_len = 0;

        while (j < line_count) {
          const char *body_line = strip_comment_prefix(lines[j]);
          if (!body_line)
            break; // Reached code

          // Stop if another marker begins
          if (strncmp(body_line, "TODO", 4) == 0 ||
              strncmp(body_line, "FIXME", 5) == 0 ||
              strncmp(body_line, "NOTE", 4) == 0 ||
              strncmp(body_line, "HACK", 4) == 0) {
            break;
          }

          char clean_line[512];
          strncpy(clean_line, body_line, sizeof(clean_line) - 1);
          clean_line[sizeof(clean_line) - 1] = '\0';
          trim_trailing(clean_line);

          size_t blen = strlen(clean_line);
          if (desc_len + blen + 2 < sizeof(desc_buf)) {
            if (desc_len > 0)
              desc_buf[desc_len++] = '\n';
            strcpy(desc_buf + desc_len, clean_line);
            desc_len += blen;
          }
          j++;
        }

        if (desc_len > 0) {
          item.description = strdup(desc_buf);
          i = j - 1; // Advance past the description block
        }
      }

      tdb_push(todos, &item);
    }
  }

  // Free line buffer
  for (size_t i = 0; i < line_count; i++)
    free(lines[i]);
  free(lines);
}
