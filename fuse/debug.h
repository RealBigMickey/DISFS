#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

/* Used for debugging, will be removed. Eventually...... */
#define LOGMSG(...) do { \
    FILE *fp = fopen("/home/bigmickey/linux2025/Linux_Final/logs.txt", "a"); \
    if (fp) { \
        fprintf(fp, __VA_ARGS__); \
        fprintf(fp, "\n"); \
        fclose(fp); \
    } \
} while (0)


#endif /* DEBUG_H */