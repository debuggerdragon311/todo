#include "./../include/Todo.h"

char *read_file(const char *path) {
  // null checking
  if (path == NULL) {
    fprintf(stderr, "Error: Invalid path pointer.\n");
    return NULL;
  }

  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    perror("Error: Opening file.\n");
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    perror("Error: fseek.\n");
    fclose(file);
    return NULL;
  }

  long fsize = ftell(file);

  if (fsize < 0) {
    perror("Error: ftell.\n");
    fclose(file);
    return NULL;
  }

  rewind(file);

  char *buff = (char *)malloc(fsize + 1);
  if (buff == NULL) {
    fprintf(stderr, "Error: memory allocation failed.\n");
    fclose(file);
    return NULL;
  }

  size_t read_bytes = fread(buff, 1, fsize, file);

  if (read_bytes != (size_t)fsize) {
    if (ferror(file)) {
      perror("Error: fread.\n");
    }
    free(buff);
    fclose(file);
    return NULL;
  }

  buff[read_bytes] = '\0';
  fclose(file);
  return buff;
}
