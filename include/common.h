#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_PATHS       8
#define MAX_K           16
#define MAX_N           32      /* MAX_K * 2 */
#define MAX_PAYLOAD     1400
#define MAX_WINDOW      16

/* Runtime log level — settable via config or SIGHUP reload.
 * 0=ERR only, 1=+WARN, 2=+INFO (default), 3=+DBG */
enum log_level { LOG_LVL_ERR = 0, LOG_LVL_WARN = 1, LOG_LVL_INFO = 2, LOG_LVL_DBG = 3 };
extern int g_log_level;

#define LOG_ERR(...)   do { fprintf(stderr, "[ERR]  " __VA_ARGS__); fputs("\n", stderr); } while (0)
#define LOG_WARN(...)  do { if (g_log_level >= LOG_LVL_WARN) { fprintf(stderr, "[WARN] " __VA_ARGS__); fputs("\n", stderr); } } while (0)
#define LOG_INFO(...)  do { if (g_log_level >= LOG_LVL_INFO) { fprintf(stderr, "[INFO] " __VA_ARGS__); fputs("\n", stderr); } } while (0)
#define LOG_DBG(...)   do { if (g_log_level >= LOG_LVL_DBG)  { fprintf(stderr, "[DBG]  " __VA_ARGS__); fputs("\n", stderr); } } while (0)

#include <sys/time.h>
static inline uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

#endif /* COMMON_H */
