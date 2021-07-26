#pragma once

#include <winsock2.h>

void usbip_forward(HANDLE hdev_src, HANDLE hdev_dst, BOOL inbound);