// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

/* [#docs]
 * BOOTSTRAP:
 *   Standard development build:
 *     cc -o build build.c
 *
 *   Minimal static-PIE binary (musl-libc, ~25 KB):
 *     musl-gcc -Os -flto -fPIE -static-pie \
 *         -ffunction-sections -fdata-sections -Wl,--gc-sections \
 *         -fno-asynchronous-unwind-tables -fno-ident \
 *         -Wl,--build-id=none -s -o build build.c
 *
 * USAGE:
 *   ./build [OPTIONS] [-- app-arguments]
 *
 *   Workflows:
 *     ./build               Incremental dev build into .cache/
 *     ./build -run          Build and execute immediately from .cache/
 *     ./build -run -- -arg  Pass flags/arguments directly to your target app
 *     ./build -release      Generate optimized standalone binary at ./todo
 *     ./build -debug        Build with debug symbols and launch in debugger
 *     ./build -clean        Purge .cache/ and all generated binaries
 *     ./build -test         Run post-build smoke tests
 *     ./build -cc <name>    Select compiler: cc (default), gcc, clang, or tcc
 *     ./build -force        Bypass timestamp cache and force a complete rebuild
 *
 * EXTENDING & CUSTOMIZING:
 *   See the "Configuration & Project Manifest" section below:
 *     - SOURCES:      List all .c translation units.
 *     - HEADERS:      List all .h files monitored for incremental rebuilds.
 *     - INCLUDE_DIRS: List header search directories (-Ipath).
 *     - LIBRARIES:    List link flags and libraries (-lm, -lpthread, etc.).
 *     - DEFINES:      List preprocessor defines (-DNAME=VALUE).
 *
 * ARCHITECTURE NOTES:
 *   > WARNING:
 *     POSIX-only implementation (Linux, macOS, BSD). Relies directly on fork(2),
 *     execvp(3), and POSIX filesystem syscalls. Native Windows (MSVC / cmd.exe)
 *     is unsupported unless run under WSL or an MSYS2/Cygwin environment.
 *
 *   - Self-Rebuilding: Compares mtime of build.c against the running binary.
 *     If source changes are detected, it recompiles and re-executes itself automatically.
 *
 *   - Isolated Caching: Intermediate and transient run binaries live inside .cache/
 *     to prevent cluttering the working directory. Only `-release` exports to the root.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

////////////////////////////////
// Configuration & Project Manifest

#define TARGET_NAME  "todo"
#define CACHE_FOLDER ".cache/"

/* 1. Source Files (.c)
 * Add any new source files here regardless of folder structure.
 * Example: "src/main.c", "src/parser.c", "src/storage.c" */
static const char *SOURCES[] = {
    "todo-tracker.c",
};

/* 2. Monitored Headers (.h)
 * Add headers here so changes to them trigger an incremental rebuild.
 * Example: "log.h", "include/parser.h", "src/config.h" */
static const char *HEADERS[] = {
    "log.h",
};

/* 3. Include Directories (-I)
 * Directories searched for #include <...> or #include "..." directives.
 * Example: ".", "include", "src" */
static const char *INCLUDE_DIRS[] = {
    ".",
};

/* 4. Libraries & Linker Flags (-l, -L)
 * Add external libraries to link against.
 * Example: "-lm", "-lpthread", "-lraylib" */
static const char *LIBRARIES[] = {
    // "-lm",
};

/* 5. Custom Preprocessor Definitions (-D)
 * Definitions passed to the preprocessor across all builds.
 * Example: "-DVERSION=\"1.0.0\"", "-DENABLE_LOGGING" */
static const char *DEFINES[] = {
    // "-DDEBUG_BUILD",
};

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

////////////////////////////////
// Dynamic Command Vector

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Cmd;

void cmd_push(Cmd *cmd, const char *arg)
{
    if (cmd->count + 1 >= cmd->capacity) {
        cmd->capacity = (cmd->capacity == 0) ? 32 : cmd->capacity * 2;
        cmd->items = realloc(cmd->items, cmd->capacity * sizeof(char*));
        if (!cmd->items) {
            fprintf(stderr, "[ERROR] Memory allocation failed\n");
            exit(1);
        }
    }
    cmd->items[cmd->count++] = arg;
    cmd->items[cmd->count] = NULL;
}

void cmd_push_many(Cmd *cmd, const char *const *args, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        cmd_push(cmd, args[i]);
    }
}

void cmd_free(Cmd *cmd)
{
    free(cmd->items);
    cmd->items = NULL;
    cmd->count = 0;
    cmd->capacity = 0;
}

////////////////////////////////
// System & Build Utilities

int run_cmd(const Cmd *cmd)
{
    printf("[CMD]");
    for (size_t i = 0; i < cmd->count; ++i) {
        printf(" %s", cmd->items[i]);
    }
    printf("\n");

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[ERROR] fork failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execvp(cmd->items[0], (char *const *)cmd->items);
        fprintf(stderr, "[ERROR] Failed to execute %s: %s\n", cmd->items[0], strerror(errno));
        exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "[ERROR] waitpid failed: %s\n", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

bool file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

time_t get_mtime(const char *path)
{
    struct stat sb;
    if (stat(path, &sb) < 0) return 0;
    return sb.st_mtime;
}

bool is_rebuild_needed(const char *target,
                       const char *const *sources, size_t source_count,
                       const char *const *headers, size_t header_count)
{
    if (!file_exists(target)) return true;
    time_t target_time = get_mtime(target);

    for (size_t i = 0; i < source_count; ++i) {
        if (get_mtime(sources[i]) > target_time) return true;
    }
    for (size_t i = 0; i < header_count; ++i) {
        if (get_mtime(headers[i]) > target_time) return true;
    }
    return false;
}

bool make_dir_if_not_exists(const char *path)
{
    struct stat sb;
    if (stat(path, &sb) == 0) {
        if (S_ISDIR(sb.st_mode)) return true;
        fprintf(stderr, "[ERROR] '%s' exists and is not a directory!\n", path);
        return false;
    }
    if (mkdir(path, 0755) < 0) {
        fprintf(stderr, "[ERROR] Failed to create directory '%s': %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

bool copy_bin_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }

    char buf[8192];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }

    fclose(in);
    fclose(out);
    chmod(dst, 0755);
    return true;
}

void auto_rebuild_self(char **argv, const char *compiler)
{
    const char *source = "build.c";
    const char *binary = argv[0];

    if (get_mtime(source) > get_mtime(binary)) {
        printf("[INFO] build.c was modified, recompiling builder...\n");
        /* NOTE(20260914-0060): Uses active compiler to prevent toolchain drift */
        Cmd cmd = {0};
        cmd_push(&cmd, compiler);
        cmd_push(&cmd, "-O0");
        cmd_push(&cmd, "-o");
        cmd_push(&cmd, binary);
        cmd_push(&cmd, source);

        int status = run_cmd(&cmd);
        cmd_free(&cmd);

        if (status != 0) {
            fprintf(stderr, "[ERROR] Failed to recompile builder\n");
            exit(1);
        }
        execvp(binary, argv);
        exit(0);
    }
}

void remove_dir_contents(const char *dir_path)
{
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *dir;
    char filepath[512];
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, dir->d_name);
        remove(filepath);
    }
    closedir(d);
    rmdir(dir_path);
}

void clean_artifacts(void)
{
    remove_dir_contents(CACHE_FOLDER);
    remove_dir_contents("bin");
    remove(TARGET_NAME);
    printf("[INFO] Cleaned build artifacts\n");
}

////////////////////////////////
// Compiler Toolchain

typedef enum {
    CC,
    GCC,
    CLANG,
    TCC,
    COMPILER_COUNT,
} Compiler;

const char *compiler_names[COMPILER_COUNT] = {
    [CC]    = "cc",
    [GCC]   = "gcc",
    [CLANG] = "clang",
    [TCC]   = "tcc",
};

bool compiler_by_name(const char *name, Compiler *compiler)
{
    for (int i = 0; i < COMPILER_COUNT; ++i) {
        if (strcmp(name, compiler_names[i]) == 0) {
            *compiler = (Compiler)i;
            return true;
        }
    }
    return false;
}

void append_compiler_flags(Cmd *cmd, Compiler compiler, bool release)
{
    cmd_push(cmd, "-Wall");
    cmd_push(cmd, "-Wextra");
    cmd_push(cmd, "-Wswitch-enum");

    /* Append all configured include paths */
    for (size_t i = 0; i < ARRAY_COUNT(INCLUDE_DIRS); ++i) {
        char *flag = malloc(strlen(INCLUDE_DIRS[i]) + 3);
        sprintf(flag, "-I%s", INCLUDE_DIRS[i]);
        cmd_push(cmd, flag);

        /*
         * (p10)
         * FIXME: flag is allocated on the heap using malloc(), but cmd_free()
         * only frees the array of pointers (free(cmd->items)), not the strings
         * themselves.
         *
         */
    }

    /* Append user-defined macros */
    for (size_t i = 0; i < ARRAY_COUNT(DEFINES); ++i) {
        cmd_push(cmd, DEFINES[i]);
    }

    if (release) {
        /* NOTE(20260914-0040): Size-optimized static-pie flags */
        cmd_push(cmd, "-Os");
        cmd_push(cmd, "-DNDEBUG");
        cmd_push(cmd, "-flto");
        cmd_push(cmd, "-fPIE");
        cmd_push(cmd, "-ffunction-sections");
        cmd_push(cmd, "-fdata-sections");
        cmd_push(cmd, "-Wl,--gc-sections");
        cmd_push(cmd, "-fno-asynchronous-unwind-tables");
        cmd_push(cmd, "-fno-unwind-tables");
        cmd_push(cmd, "-fno-ident");
        cmd_push(cmd, "-Wl,--build-id=none");
        cmd_push(cmd, "-s");

        if (compiler != TCC) {
            cmd_push(cmd, "-static-pie");
        }
    } else {
        cmd_push(cmd, "-O0");
        cmd_push(cmd, "-ggdb");
        if (compiler == CLANG) {
            cmd_push(cmd, "-fsanitize=address,undefined");
        }
    }
}

////////////////////////////////
// CLI Parser

void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [OPTIONS] [-- app-arguments]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -release       Build standalone release binary (./%s)\n", TARGET_NAME);
    fprintf(stderr, "  -force         Force compilation even if target is up-to-date\n");
    fprintf(stderr, "  -test          Run smoke test after building\n");
    fprintf(stderr, "  -run           Run fast tmp binary from cache (keeps root clean)\n");
    fprintf(stderr, "  -debug         Run application in debugger (gf2)\n");
    fprintf(stderr, "  -clean         Remove .cache/, leftover bin/, and root binary\n");
    fprintf(stderr, "  -cc <compiler> Compiler to use (cc, gcc, clang, tcc) [default: cc]\n");
    fprintf(stderr, "  -help, -h      Show this help message\n");
}

////////////////////////////////
// Main Entry

int main(int argc, char **argv)
{
    bool release = false;
    bool force = false;
    bool test = false;
    bool run = false;
    bool debug = false;
    bool clean = false;
    const char *compiler_name = "cc";

    int app_argc = 0;
    char **app_argv = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-release") == 0 || strcmp(arg, "--release") == 0) {
            release = true;
        } else if (strcmp(arg, "-force") == 0 || strcmp(arg, "--force") == 0) {
            force = true;
        } else if (strcmp(arg, "-test") == 0 || strcmp(arg, "--test") == 0) {
            test = true;
        } else if (strcmp(arg, "-run") == 0 || strcmp(arg, "--run") == 0) {
            run = true;
        } else if (strcmp(arg, "-debug") == 0 || strcmp(arg, "--debug") == 0) {
            debug = true;
        } else if (strcmp(arg, "-clean") == 0 || strcmp(arg, "--clean") == 0) {
            clean = true;
        } else if (strcmp(arg, "-help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(arg, "-cc") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "[ERROR] Missing compiler name after -cc\n");
                return 1;
            }
            compiler_name = argv[i];
        } else if (strcmp(arg, "--") == 0) {
            app_argv = &argv[i + 1];
            app_argc = argc - (i + 1);
            break;
        } else {
            fprintf(stderr, "[ERROR] Unknown argument: %s\n", arg);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Auto-rebuild with currently selected compiler */
    auto_rebuild_self(argv, compiler_name);

    if (clean) {
        clean_artifacts();
        return 0;
    }

    Compiler compiler;
    if (!compiler_by_name(compiler_name, &compiler)) {
        fprintf(stderr, "[ERROR] Unknown compiler \"%s\"\n", compiler_name);
        return 1;
    }

    if (!make_dir_if_not_exists(CACHE_FOLDER)) return 1;

    /* NOTE(20260914-0050): Use isolated cache binaries to keep project root clean */
    const char *target_bin = CACHE_FOLDER "todo.tmp";
    if (release) {
        target_bin = CACHE_FOLDER "todo.release";
    } else if (debug) {
        target_bin = CACHE_FOLDER "todo.debug";
    }

    bool needs_build = force || is_rebuild_needed(target_bin,
                                                  SOURCES, ARRAY_COUNT(SOURCES),
                                                  HEADERS, ARRAY_COUNT(HEADERS));

    if (needs_build) {
        Cmd cmd = {0};
        cmd_push(&cmd, compiler_names[compiler]);

        append_compiler_flags(&cmd, compiler, release);

        cmd_push(&cmd, "-o");
        cmd_push(&cmd, target_bin);

        /* Append all source files */
        cmd_push_many(&cmd, SOURCES, ARRAY_COUNT(SOURCES));

        /* Append any external libraries (-l...) */
        cmd_push_many(&cmd, LIBRARIES, ARRAY_COUNT(LIBRARIES));

        int status = run_cmd(&cmd);
        cmd_free(&cmd);

        if (status != 0) return status;

        if (release) {
            copy_bin_file(target_bin, TARGET_NAME);
            printf("[INFO] Created production binary: ./%s\n", TARGET_NAME);
        }
    } else {
        printf("[INFO] %s is up-to-date\n", target_bin);
    }

    if (test) {
        printf("[INFO] Running smoke test...\n");
        Cmd test_cmd = {0};
        cmd_push(&test_cmd, target_bin);
        cmd_push(&test_cmd, "--help");

        int status = run_cmd(&test_cmd);
        cmd_free(&test_cmd);

        if (status != 0) return status;
        printf("[INFO] Smoke test passed\n");
    }

    if (run) {
        Cmd app_cmd = {0};
        if (debug) cmd_push(&app_cmd, "gf2");
        cmd_push(&app_cmd, target_bin);

        for (int i = 0; i < app_argc; ++i) {
            cmd_push(&app_cmd, app_argv[i]);
        }

        int exit_code = run_cmd(&app_cmd);
        cmd_free(&app_cmd);
        return exit_code;
    }

    return 0;
}
