#pragma once

#include <string.h>
#include <stdio.h>
#define PORT 5050

const char *get_server_url();

int change_server_ip(const char *ip);
int change_server_url(const char *new_url);