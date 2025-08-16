#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// Set DEBUG_MODE to 1 to enable logging, 0 to disable
#define DEBUG_MODE 1

/* Used for debugging, will be removed. Eventually...... */
#if DEBUG_MODE
/* Define your own destination path if you wish to test */
#define LOGMSG(...) do { \
    FILE *fp = fopen("/home/bigmickey/linux2025/DISFS/logs.txt", "a"); \
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