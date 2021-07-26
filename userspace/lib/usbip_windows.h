#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "getopt.h"

int init_socket(void);
int cleanup_socket(void);
