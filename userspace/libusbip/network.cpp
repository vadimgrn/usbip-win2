/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2022-2023 Vadym Hrynchyshyn
 */

#include "network.h"
#include "dbgcode.h"

#include <usbip\proto_op.h>

#include <ws2tcpip.h>
#include <mstcpip.h>

#include <spdlog\spdlog.h>

static int usbip_net_xmit(SOCKET sockfd, void *buff, size_t bufflen, int sending)
{
	int total = 0;

	if (!bufflen)
		return 0;

	do {
		int nbytes;

		if (sending)
			nbytes = send(sockfd, (char*)buff, (int)bufflen, 0);
		else
			nbytes = recv(sockfd, (char*)buff, (int)bufflen, 0);

		if (nbytes <= 0)
			return -1;

		buff	= (void *) ((intptr_t) buff + nbytes);
		bufflen	-= nbytes;
		total	+= nbytes;
	} while (bufflen > 0);

	return total;
}

int usbip_net_recv(SOCKET sockfd, void *buff, size_t bufflen)
{
	return usbip_net_xmit(sockfd, buff, bufflen, 0);
}

int usbip_net_send(SOCKET sockfd, void *buff, size_t bufflen)
{
	return usbip_net_xmit(sockfd, buff, bufflen, 1);
}

int usbip_net_send_op_common(SOCKET sockfd, uint16_t code, uint32_t status)
{
        op_common op_common{};

	op_common.version = USBIP_VERSION;
	op_common.code    = code;
	op_common.status  = status;

	PACK_OP_COMMON(1, &op_common);

	auto rc = usbip_net_send(sockfd, &op_common, sizeof(op_common));
	if (rc < 0) {
		spdlog::error("usbip_net_send error #{}", rc);
		return -1;
	}

	return 0;
}

int usbip_net_recv_op_common(SOCKET sockfd, uint16_t *code, int *pstatus)
{
	struct op_common op_common;
	int rc;

	memset(&op_common, 0, sizeof(op_common));

	rc = usbip_net_recv(sockfd, &op_common, sizeof(op_common));
	if (rc < 0) {
		spdlog::error("usbip_net_recv error #{}", rc);
		return ERR_NETWORK;
	}

	PACK_OP_COMMON(0, &op_common);

	if (op_common.version != USBIP_VERSION) {
		spdlog::error("version mismatch: {} != {}", op_common.version, USBIP_VERSION);
		return ERR_VERSION;
	}

	switch (*code) {
	case OP_UNSPEC:
		break;
	default:
		if (op_common.code != *code) {
			spdlog::error("unexpected pdu {:#0x} for {:#0x}", op_common.code, *code);
			return ERR_PROTOCOL;
		}
	}

	*pstatus = op_common.status;

	if (op_common.status != ST_OK) {
		spdlog::error("request failed: status: {}", dbg_opcode_status(op_common.status));
		return ERR_STATUS;
	}

	*code = op_common.code;
	return 0;
}

int usbip_net_set_reuseaddr(SOCKET sockfd)
{
	const int val = 1;
	int ret;

	ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));
	if (ret < 0) {
		spdlog::error("setsockopt: SO_REUSEADDR");
	}

	return ret;
}

int usbip_net_set_nodelay(SOCKET sockfd)
{
	int val = 1;
	int ret;

	ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&val, sizeof(val));
	if (ret < 0) {
		spdlog::error("setsockopt: TCP_NODELAY");
	}

	return ret;
}

unsigned
get_keepalive_timeout(void)
{
	char	env_timeout[32];
	unsigned	timeout;
	size_t	reqsize;

	if (getenv_s(&reqsize, env_timeout, 32, "KEEPALIVE_TIMEOUT") != 0) {
		return 0;
	}

	if (sscanf_s(env_timeout, "%u", &timeout) == 1) {
		return timeout;
	}

	return 0;
}

int usbip_net_set_keepalive(SOCKET sockfd)
{
	unsigned	timeout;

	timeout = get_keepalive_timeout();
	if (timeout > 0) {
		struct tcp_keepalive	keepalive;
		DWORD	outlen;
		int	ret;

		/* windows tries 10 times every keepaliveinterval */
		keepalive.onoff = 1;
		keepalive.keepalivetime = timeout * 1000 / 2;
		keepalive.keepaliveinterval = timeout * 1000 / 10 / 2;

		ret = WSAIoctl(sockfd, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), nullptr, 0, &outlen, nullptr, nullptr);
		if (ret) {
			spdlog::error("failed to set KEEPALIVE via SIO_KEEPALIVE_VALS: {:#x}", GetLastError());
			return -1;
		}
		return 0;
	}
	else {
		DWORD	val = 1;
		int	ret;

		ret = setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&val, sizeof(val));
		if (ret < 0) {
			spdlog::error("failed to set KEEPALIVE via setsockopt: {:#x}", GetLastError());
		}
		return ret;
	}
}

int usbip_net_set_v6only(SOCKET sockfd)
{
	int val = 1;
	int ret;

	ret = setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&val, sizeof(val));
	if (ret < 0) {
		spdlog::error("setsockopt: IPV6_V6ONLY");
	}

	return ret;
}

/*
 * IPv6 Ready
 */
usbip::Socket usbip_net_tcp_connect(const char *hostname, const char *port)
{
        usbip::Socket sock;
        struct addrinfo hints, *res, *rp;
	int ret;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	/* get all possible addresses */
	ret = getaddrinfo(hostname, port, &hints, &res);
	if (ret < 0) {
		spdlog::error("getaddrinfo(host='{}', port='{}') error #{} {}", hostname, port, ret, gai_strerrorA(ret));
		return sock;
	}

	/* try the addresses */
	for (rp = res; rp; rp = rp->ai_next) {

                sock.reset(socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
                if (!sock) {
                        continue;
                }

		/* should set TCP_NODELAY for usbip */
		usbip_net_set_nodelay(sock.get());
		/* TODO: write code for heartbeat */
		usbip_net_set_keepalive(sock.get());

		if (connect(sock.get(), rp->ai_addr, (int)rp->ai_addrlen) == 0)
			break;

		sock.close();
	}

	freeaddrinfo(res);

        if (!rp) {
                sock.close();
        }

	return sock;
}
