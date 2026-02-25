/*
 * By default, DISFS assumes the backend server is hosted locally at 127.0.0.1:5050.
 * The long-term vision is to host the server centrally, allowing users to
 * plug and use the system without self-hosting any components.
 */

#include "server_config.h"
static char server_url[128] = "http://0.0.0.0:5050"; 

const char *get_server_url() {
    return server_url;
}

#define U8_BIT_RANGE(x) (0 <= x  && x<= 255)
#define IS_ASCII_NUMBER(x) ('0' <= x && x<= '9')

/* 0 -> success, -1 -> Bad format */
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
    snprintf(server_url, sizeof(server_url), "http://%s:%d", ip, PORT);
    server_url[sizeof(server_url) - 1] = '\0';  // Just in case
    return 0;
}



/* 0 -> success, -1 -> Bad format
 * Though crude, checks for any '/' characters past https:// to block injections
 * Only accepts https for urls 
 */
int change_server_url(const char *new_url) {
    if (!new_url)
        return -1;
    
    const char *p = new_url;
    if (*p == '\0')
        return -1;
    if (strchr(p, '/') != NULL)
        return -1;
    if (strlen(new_url) >= sizeof(server_url))
        return -1;
    snprintf(server_url, sizeof(server_url), "https://%s", new_url);
    return 0;
}