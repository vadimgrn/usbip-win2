#pragma once

#include <basetsd.h>
#include <ntdef.h>

namespace wsk 
{
        struct SOCKET;
}

bool usbip_net_send(wsk::SOCKET *sock, void *data, ULONG len);
bool usbip_net_recv(wsk::SOCKET *sock, void *data, ULONG len);

bool usbip_net_recv_op_common(wsk::SOCKET *sock, UINT16 response_code, UINT32 *status = nullptr);
