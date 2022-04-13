/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/
#pragma once

#include <WinSock2.h>

inline const char usbip_xfer_binary[] = "usbip_xfer.exe"; 

struct usbip_xfer_args
{
	HANDLE hdev;
	WSAPROTOCOL_INFOW info;
	bool client; // false -> server
};
