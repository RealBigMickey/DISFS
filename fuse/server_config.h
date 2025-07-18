#pragma once

#include <string.h>
#include <stdio.h>
#define PORT 5050

const char *get_server_ip();

int change_server_ip(const char *ip);