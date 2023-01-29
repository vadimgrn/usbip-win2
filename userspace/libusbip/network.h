/*
 * Copyright (C) 2021-2023 Vadym Hrynchyshyn
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#pragma once

#include "win_socket.h"

namespace usbip::net
{

bool recv(SOCKET s, void *buf, size_t len, bool *eof = nullptr);
bool send(SOCKET s, const void *buf, size_t len);

bool send_op_common(SOCKET s, uint16_t code);
int recv_op_common(SOCKET s, uint16_t expected_code);

Socket tcp_connect(const char *hostname, const char *service);

} // namespace usbip::net
