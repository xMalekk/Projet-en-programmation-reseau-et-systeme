// Implémentation simple pour la journalisation du démon.
// Fournit log_info/log_warn/log_error/log_debug avec time-stamp.

#define _GNU_SOURCE

#include "medievail_net/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static int g_verbose = 0;

static void vlogf(const char *label, const char *fmt, va_list args) {
    time_t now = time(NULL);
    struct tm tm_info;
#if defined(_WIN32)
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif
    char buff[32];
    strftime(buff, sizeof(buff), "%H:%M:%S", &tm_info);
    fprintf(stderr, "[%s] %s | ", label, buff);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void log_init(int verbose_mode) {
    g_verbose = verbose_mode ? 1 : 0;
}

void log_set_verbose(int enabled) {
    g_verbose = enabled ? 1 : 0;
}

void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf("INFO", fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf("WARN", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf("ERROR", fmt, args);
    va_end(args);
}

void log_debug(const char *fmt, ...) {
    if (!g_verbose) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vlogf("DEBUG", fmt, args);
    va_end(args);
}
