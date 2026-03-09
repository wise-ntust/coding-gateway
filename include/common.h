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

/* Log macros — always called with at least one format argument */
#define LOG_INFO(...)  fprintf(stderr, "[INFO] " __VA_ARGS__), fprintf(stderr, "\n")
#define LOG_WARN(...)  fprintf(stderr, "[WARN] " __VA_ARGS__), fprintf(stderr, "\n")
#define LOG_ERR(...)   fprintf(stderr, "[ERR]  " __VA_ARGS__), fprintf(stderr, "\n")

#ifdef DEBUG
#define LOG_DBG(...)   fprintf(stderr, "[DBG]  " __VA_ARGS__), fprintf(stderr, "\n")
#else
#define LOG_DBG(...)   ((void)0)
#endif

#endif /* COMMON_H */
