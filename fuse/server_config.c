/*
 * By default, DISFS assumes the backend server is hosted locally at 127.0.0.1:5050.
 * The long-term vision is to host the server centrally, allowing users to
 * plug and use the system without self-hosting any components.
 */

#include "server_config.h"
static char server_ip[64] = "127.0.0.1:5050"; 

const char *get_server_ip() {
    return server_ip;
}

#define U8_BIT_RANGE(x) (0 <= x  && x<= 255)
#define IS_ASCII_NUMBER(x) ('0' <= x && x<= '9')

/*0 -> success, -1 -> Bad format */
int change_server_ip(const char *ip) {
    const char *p = ip;

    int dot_count = 0;
    int digit_count = 0;
    int num = 0;
    for (int i = 0; i < 64; p++, i++) {
        char c = *p;
        if (c == '\0')
            break;
        if (c == '.') {
            digit_count = 0;
            dot_count++;
            num = 0;
        } else if (IS_ASCII_NUMBER(c)) {
            digit_count++;
            num *= 10;
            num += c - '0';
    
            if (!U8_BIT_RANGE(num) || digit_count > 3)
                return -1;
        } else 
            return -1;
    }
    if (dot_count != 3)
        return -1;
    snprintf(server_ip, sizeof(server_ip), "%s:%d", ip, PORT);
    server_ip[sizeof(server_ip) - 1] = '\0';  // Just in case
    return 0;
}

