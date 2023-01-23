/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2022-2023 Vadym Hrynchyshyn
 */

#pragma once

#include "win_socket.h"
#include <usbip\consts.h>

namespace libusbip::net
{

bool recv(SOCKET sockfd, void *buf, size_t len);
bool send(SOCKET sockfd, const void *buf, size_t len);

bool send_op_common(SOCKET sockfd, uint16_t code, uint32_t status);
err_t recv_op_common(SOCKET sockfd, uint16_t *code, int *pstatus);

bool set_reuseaddr(SOCKET sockfd);
bool set_nodelay(SOCKET sockfd);
bool set_keepalive(SOCKET sockfd);

Socket tcp_connect(const char *hostname, const char *port);

} // namespace libusbip::net
