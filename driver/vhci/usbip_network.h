#pragma once

#include "wsk_utils.h"

bool usbip_net_recv(wsk::SOCKET *sock, void *buf, size_t len);
bool usbip_net_send(wsk::SOCKET *sock, void *buf, size_t len);

bool usbip_net_send_op_common(wsk::SOCKET *sock, UINT16 code, UINT32 status);
bool usbip_net_recv_op_common(wsk::SOCKET *sock, UINT16 &code, UINT32 &status);
