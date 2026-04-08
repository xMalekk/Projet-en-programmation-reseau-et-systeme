// Interface de journalisation partagée par le démon.
// Fournira des helpers type log_info/log_warn/log_error/log_debug.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void log_init(int verbose_mode);
void log_set_verbose(int enabled);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_debug(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
