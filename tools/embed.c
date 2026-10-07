// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

#include <stdio.h>
#include <stdlib.h>

static void embed(FILE *out, const char *path, const char *var_name) {
    FILE *in = fopen(path, "rb");
    if (!in) { perror(path); exit(1); }

    fprintf(out, "static const unsigned char _raw_%s[] = {\n", var_name);
    int c, n = 0;
    size_t len = 0;
    while ((c = fgetc(in)) != EOF) {
        fprintf(out, "0x%02x, ", c);
        if (++n == 16) { fprintf(out, "\n"); n = 0; }
        len++;
    }
    fprintf(out, "0x00\n};\n"); // null-terminator safety
    fprintf(out, "const char *%s = (const char *)_raw_%s;\n", var_name, var_name);
    fprintf(out, "const size_t %s_len = %zu;\n\n", var_name, len);
    fclose(in);
}

int main(int argc, char **argv) {
    if (argc < 4 || (argc - 2) % 2 != 0) return 1;
    FILE *out = fopen(argv[1], "w");
    if (!out) return 1;

    fprintf(out, "#include <stddef.h>\n\n");
    for (int i = 2; i < argc; i += 2) {
        embed(out, argv[i], argv[i + 1]);
    }
    fclose(out);
    return 0;
}
