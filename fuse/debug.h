#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// Set DEBUG_MODE to 1 to enable logging, 0 to disable
#define DEBUG_MODE 1
#define DISPLAY_CACHE_STATUS 1


/* Used for debugging, will be removed. Eventually...... */
#if DEBUG_MODE
#define LOGMSG(...) do { \
    FILE *fp = fopen(logs_debug_path, "a"); \
    if (fp) { \
        fprintf(fp, __VA_ARGS__); \
        fprintf(fp, "\n"); \
        fclose(fp); \
    } \
} while (0)

#else

#define LOGMSG(...) do { } while (0)

#endif

#define LOGCACHE(...) do { \
    FILE *fp = fopen(cache_debug_path, "w"); \
    if (fp) { \
            fprintf(fp, __VA_ARGS__); \
            fprintf(fp, "\n"); \
            fclose(fp); \
        } \
} while (0)


#endif