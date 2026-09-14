#ifndef LOG_H_INCLUDE_GUARD
#define LOG_H_INCLUDE_GUARD

////////////////////////////////
// LOG.H - v1.0 - Public Domain / MIT
// Scalable, high-performance console logging library in pure C.
//
// USAGE:
//   In EXACTLY ONE C or C++ file, define this before including:
//       #define LOG_IMPLEMENTATION
//       #include "log.h"
//
//   In all other files, simply do:
//       #include "log.h"
//
// CONFIGURABLE MACROS:
//   LOG_DEF               Storage specifier (default: extern / extern "C")
//   LOG_STACK_BUF_SIZE    Stack buffer threshold in bytes (default: 1024)
//   LOG_MALLOC(sz)        Custom allocator override (default: malloc)
//   LOG_FREE(ptr)         Custom free override (default: free)
////////////////////////////////

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
    #endif

    ////////////////////////////////
    // LINKAGE CONFIGURATION
    #ifndef LOG_DEF
    #define LOG_DEF extern
    #endif

    ////////////////////////////////
    // COMPILE-TIME PRINTF CHECKING
    #if defined(__GNUC__) || defined(__clang__)
    #define LOG_PRINTF_ATTR(fmt, args) __attribute__((format(printf, fmt, args)))
    #else
    #define LOG_PRINTF_ATTR(fmt, args)
    #endif

    ////////////////////////////////
    // ANSI COLOR CODES
    #define ANSI_RESET       "\033[0m"
    #define ANSI_BOLD        "\033[1m"
    #define ANSI_DIM         "\033[2m"
    #define ANSI_RED         "\033[31m"
    #define ANSI_GREEN       "\033[32m"
    #define ANSI_YELLOW      "\033[33m"
    #define ANSI_BLUE        "\033[34m"
    #define ANSI_MAGENTA     "\033[35m"
    #define ANSI_CYAN        "\033[36m"
    #define ANSI_WHITE       "\033[37m"

    #define ANSI_BOLD_RED    "\033[1;31m"
    #define ANSI_BOLD_GREEN  "\033[1;32m"
    #define ANSI_BOLD_YELLOW "\033[1;33m"
    #define ANSI_BOLD_BLUE   "\033[1;34m"
    #define ANSI_BOLD_CYAN   "\033[1;36m"

    ////////////////////////////////
    // PUBLIC API
    LOG_PRINTF_ATTR(1, 2) LOG_DEF void log_info(const char *fmt, ...);
    LOG_PRINTF_ATTR(1, 2) LOG_DEF void log_warn(const char *fmt, ...);
    LOG_PRINTF_ATTR(1, 2) LOG_DEF void log_error(const char *fmt, ...);
    LOG_PRINTF_ATTR(1, 2) LOG_DEF void log_success(const char *fmt, ...);

    // Log to any custom stream (FILE*)
    LOG_PRINTF_ATTR(4, 5) LOG_DEF void log_custom(FILE *stream, const char *prefix_colored, const char *prefix_plain, const char *fmt, ...);

    // Optional initialization for strict multi-threaded startup safety
    LOG_DEF void log_init(void);

    // Override color output: 1 = enable, 0 = disable, -1 = auto-detect TTY
    LOG_DEF void log_set_color_mode(int mode);

    #ifdef __cplusplus
}
#endif

#endif // LOG_H_INCLUDE_GUARD

////////////////////////////////
// IMPLEMENTATION
#ifdef LOG_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

////////////////////////////////
// ALLOCATORS AND BUFFER LIMITS
#ifndef LOG_MALLOC
#define LOG_MALLOC(sz) malloc(sz)
#endif
#ifndef LOG_FREE
#define LOG_FREE(ptr)  free(ptr)
#endif
#ifndef LOG_STACK_BUF_SIZE
#define LOG_STACK_BUF_SIZE 1024
#endif

////////////////////////////////
// PLATFORM TTY DETECTION
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#define LOG_PLATFORM_ISATTY(stream) _isatty(_fileno(stream))
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#define LOG_PLATFORM_ISATTY(stream) isatty(fileno(stream))
#else
#define LOG_PLATFORM_ISATTY(stream) 1
#endif

static int log_g_color_mode = -1; // -1 = auto, 0 = off, 1 = on

static int log_detect_tty(FILE *stream) {
    if (log_g_color_mode != -1) {
        return log_g_color_mode;
    }
    int is_tty = LOG_PLATFORM_ISATTY(stream);
    #if defined(_WIN32)
    if (is_tty) {
        HANDLE hOut = (stream == stderr) ? GetStdHandle(STD_ERROR_HANDLE) : GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(hOut, &mode)) {
                SetConsoleMode(hOut, mode | 0x0004); // ENABLE_VIRTUAL_TERMINAL_PROCESSING
            }
        }
    }
    #endif
    return is_tty;
}

void log_set_color_mode(int mode) {
    log_g_color_mode = mode;
}

void log_init(void) {
    (void)log_detect_tty(stdout);
    (void)log_detect_tty(stderr);
}

////////////////////////////////
// CORE FORMATTING AND WRITE ENGINE
static void log_write_v(FILE *stream, const char *prefix_col, const char *prefix_plain, const char *fmt, va_list args) {
    char stack_buf[LOG_STACK_BUF_SIZE];
    char *buf = stack_buf;
    size_t buf_cap = sizeof(stack_buf);

    const int use_color = log_detect_tty(stream);
    const char *prefix = use_color ? prefix_col : prefix_plain;
    size_t prefix_len = strlen(prefix);

    memcpy(buf, prefix, prefix_len);

    va_list args_copy;
    va_copy(args_copy, args);

    // Fast-path: format into stack buffer without dynamic allocation
    int written = vsnprintf(buf + prefix_len, buf_cap - prefix_len, fmt, args);
    if (written < 0) {
        va_end(args_copy);
        return;
    }

    size_t needed = prefix_len + (size_t)written + 2; // Room for '\n' and '\0'

    // Slow-path fallback: heap allocate only when log exceeds stack buffer size
    if (needed > buf_cap) {
        buf = (char *)LOG_MALLOC(needed);
        if (!buf) {
            va_end(args_copy);
            return;
        }
        memcpy(buf, prefix, prefix_len);
        vsnprintf(buf + prefix_len, needed - prefix_len, fmt, args_copy);
    }
    va_end(args_copy);

    size_t total_len = prefix_len + (size_t)written;

    // Normalize newlines (auto-append newline only if omitted by caller)
    if (total_len == prefix_len || buf[total_len - 1] != '\n') {
        buf[total_len++] = '\n';
        buf[total_len] = '\0';
    }

    // Atomic write call: avoids cross-thread text interleaving
    fwrite(buf, 1, total_len, stream);

    if (buf != stack_buf) {
        LOG_FREE(buf);
    }
}

////////////////////////////////
// PUBLIC FUNCTION IMPLEMENTATIONS
void log_custom(FILE *stream, const char *prefix_col, const char *prefix_plain, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_v(stream, prefix_col, prefix_plain, fmt, args);
    va_end(args);
}

void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_v(stdout, ANSI_BOLD_CYAN "info:" ANSI_RESET " ", "info: ", fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_v(stderr, ANSI_BOLD_YELLOW "warn:" ANSI_RESET " ", "warn: ", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_v(stderr, ANSI_BOLD_RED "error:" ANSI_RESET " ", "error: ", fmt, args);
    va_end(args);
}

void log_success(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_v(stdout, ANSI_BOLD_GREEN "done:" ANSI_RESET " ", "done: ", fmt, args);
    va_end(args);
}

#endif // LOG_IMPLEMENTATION
