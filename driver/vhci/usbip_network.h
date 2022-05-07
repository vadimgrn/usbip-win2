#pragma once

#include "usbip_api_consts.h"
#include "mdl_cpp.h"
#include "wsk_cpp.h"

namespace usbip
{

using wsk::SOCKET;

bool send(SOCKET *sock, memory pool, void *data, ULONG len);
bool recv(SOCKET *sock, memory pool, void *data, ULONG len);

err_t recv_op_common(SOCKET *sock, UINT16 expected_code, op_status_t &status);

} // namespace usbip
