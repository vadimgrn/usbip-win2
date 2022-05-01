#pragma once

#include "mdl_cpp.h"
#include "wsk_cpp.h"

namespace usbip
{

using wsk::SOCKET;

bool send(SOCKET *sock, memory pool, void *data, ULONG len);
bool receive(SOCKET *sock, memory pool, void *data, ULONG len);

bool receive_op_common(SOCKET *sock, UINT16 response_code, UINT32 *status = nullptr);

inline WSK_BUF make_buffer(const Mdl &mdl)
{
        return { mdl.get(), 0, mdl.size() };
}

} // namespace usbip
