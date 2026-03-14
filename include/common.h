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

/* First argument must be a string literal (format string); remaining args are
 * printf-style values.  e.g. LOG_INFO("msg") or LOG_INFO("val: %d", x)  */
#define LOG_INFO(...)  do { fprintf(stderr, "[INFO] " __VA_ARGS__); fputs("\n", stderr); } while (0)
#define LOG_WARN(...)  do { fprintf(stderr, "[WARN] " __VA_ARGS__); fputs("\n", stderr); } while (0)
#define LOG_ERR(...)   do { fprintf(stderr, "[ERR]  " __VA_ARGS__); fputs("\n", stderr); } while (0)

#include <sys/time.h>
static inline uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

#ifdef DEBUG
#define LOG_DBG(...)   do { fprintf(stderr, "[DBG]  " __VA_ARGS__); fputs("\n", stderr); } while (0)
#else
#define LOG_DBG(...)   do { if (0) fprintf(stderr, "" __VA_ARGS__); } while (0)
#endif

#endif /* COMMON_H */
