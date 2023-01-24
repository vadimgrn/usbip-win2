/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2022-2023 Vadym Hrynchyshyn
 */

#pragma once

#include "win_socket.h"
#include <usbip\consts.h>

namespace usbip::net
{

bool recv(SOCKET s, void *buf, size_t len, bool *eof = nullptr);
bool send(SOCKET s, const void *buf, size_t len);

bool send_op_common(SOCKET s, uint16_t code);
err_t recv_op_common(SOCKET s, uint16_t expected_code, op_status_t *status = nullptr);

Socket tcp_connect(const char *hostname, const char *service);

} // namespace usbip::net
