/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2022-2023 Vadym Hrynchyshyn
 */

#pragma once

#include "win_socket.h"

inline const char *usbip_port = "3240";
void usbip_setup_port_number(const char *arg);

int usbip_net_recv(SOCKET sockfd, void *buff, size_t bufflen);
int usbip_net_send(SOCKET sockfd, void *buff, size_t bufflen);
int usbip_net_send_op_common(SOCKET sockfd, uint16_t code, uint32_t status);
int usbip_net_recv_op_common(SOCKET sockfd, uint16_t *code, int *pstatus);
int usbip_net_set_reuseaddr(SOCKET sockfd);
int usbip_net_set_nodelay(SOCKET sockfd);
int usbip_net_set_keepalive(SOCKET sockfd);
int usbip_net_set_v6only(SOCKET sockfd);

usbip::Socket usbip_net_tcp_connect(const char *hostname, const char *port);
