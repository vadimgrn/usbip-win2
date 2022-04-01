#pragma once

#include <winsock2.h>
#include <windows.h>

#include "usbip_common.h"

int recv_request_import(SOCKET sockfd);
int recv_request_devlist(SOCKET connfd);