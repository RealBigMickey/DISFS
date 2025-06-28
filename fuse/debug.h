#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// Set DEBUG_MODE to 1 to enable logging, 0 to disable
#define DEBUG_MODE 0

/* Used for debugging, will be removed. Eventually...... */
#if DEBUG_MODE
#define LOGMSG(...) do { \
    FILE *fp = fopen("/home/bigmickey/linux2025/Linux_Final/logs.txt", "a"); \
    if (fp) { \
        fprintf(fp, __VA_ARGS__); \
        fprintf(fp, "\n"); \
        fclose(fp); \
    } \
} while (0)
#else
#define LOGMSG(...) do { } while (0)
#endif


#endif /* DEBUG_H */