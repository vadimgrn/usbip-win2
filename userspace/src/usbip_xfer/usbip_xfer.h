/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/
#pragma once

#include <WinSock2.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

__inline const char *usbip_xfer_binary() 
{ 
	return "usbip_xfer.exe"; 
}

struct usbip_xfer_args
{
	HANDLE hdev;
	WSAPROTOCOL_INFOW info;
	bool client; // false -> server
};

#ifdef __cplusplus
}
#endif
