#pragma once

#include <stdbool.h>
#include <WinSock2.h>

int start_xfer(HANDLE hdev, SOCKET sockfd, bool client);