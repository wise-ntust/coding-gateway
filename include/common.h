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

/* Log macros — fmt must be a string literal; ##__VA_ARGS__ is a GNU extension
 * universally supported by GCC and Clang (the only compilers used here). */
#define LOG_INFO(fmt, ...)  do { fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)  do { fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERR(fmt, ...)   do { fprintf(stderr, "[ERR]  " fmt "\n", ##__VA_ARGS__); } while (0)

#ifdef DEBUG
#define LOG_DBG(fmt, ...)   do { fprintf(stderr, "[DBG]  " fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define LOG_DBG(fmt, ...)   do { (void)(fmt); } while (0)
#endif

#endif /* COMMON_H */
