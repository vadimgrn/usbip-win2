/*
* Copyright (C) 2022-2023 Vadym Hrynchyshyn
* Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "network.h"
#include "dbgcode.h"

#include <usbip\proto_op.h>

#include <ws2tcpip.h>
#include <mstcpip.h>

#include <spdlog\spdlog.h>

namespace
{

auto xmit(SOCKET sockfd, void *buf, size_t len, bool sending)
{
	while (len) {
		auto ret = sending ? send(sockfd, static_cast<const char*>(buf), int(len), 0) :
			             recv(sockfd, static_cast<char*>(buf), int(len), 0);

		if (ret > 0) {
			buf = static_cast<char*>(buf) + ret;
			len -= ret;
		} else { // !ret -> EOF
			static_assert(SOCKET_ERROR < 0);
			return false;
		}
	}

	return true;
}

unsigned int get_keepalive_timeout()
{
	char env_timeout[32];
	size_t reqsize;

	if (getenv_s(&reqsize, env_timeout, sizeof(env_timeout), "KEEPALIVE_TIMEOUT")) { // error
		return 0;
	}

	unsigned int timeout;

	if (sscanf_s(env_timeout, "%u", &timeout) == 1) {
		return timeout;
	}

	return 0;
}

} // namespace


bool libusbip::net::recv(SOCKET sockfd, void *buf, size_t len)
{
	return xmit(sockfd, buf, len, false);
}

bool libusbip::net::send(SOCKET sockfd, const void *buf, size_t len)
{
	return xmit(sockfd, const_cast<void*>(buf), len, true);
}

bool libusbip::net::send_op_common(SOCKET sockfd, uint16_t code, uint32_t status)
{
        op_common r {
		.version = USBIP_VERSION,
		.code = code,
		.status = status,
	};

	PACK_OP_COMMON(1, &r);
	return send(sockfd, &r, sizeof(r));
}

err_t libusbip::net::recv_op_common(SOCKET sockfd, uint16_t *code, int *pstatus)
{
	op_common r;

	if (recv(sockfd, &r, sizeof(r))) {
		PACK_OP_COMMON(0, &r);
	} else {
		return ERR_NETWORK;
	}

	if (r.version != USBIP_VERSION) {
		spdlog::error("version mismatch: {} != {}", r.version, USBIP_VERSION);
		return ERR_VERSION;
	}

	switch (*code) {
	case OP_UNSPEC:
		break;
	default:
		if (r.code != *code) {
			spdlog::error("unexpected pdu {:#0x} for {:#0x}", r.code, *code);
			return ERR_PROTOCOL;
		}
	}

	*pstatus = r.status;

	if (r.status != ST_OK) {
		spdlog::error("request failed: status: {}", libusbip::dbg_opcode_status(r.status));
		return ERR_STATUS;
	}

	*code = r.code;
	return ERR_NONE;
}

bool libusbip::net::set_reuseaddr(SOCKET sockfd)
{
	int val = 1;

	auto err = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&val, sizeof(val));
	if (err) {
		spdlog::error("setsockopt(SO_REUSEADDR): WSAGetLastError {}", WSAGetLastError());
	}

	return !err;
}

bool libusbip::net::set_nodelay(SOCKET sockfd)
{
	int val = 1;

	auto err = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&val, sizeof(val));
	if (err) {
		spdlog::error("setsockopt(TCP_NODELAY): WSAGetLastError {}", WSAGetLastError());
	}

	return !err;
}

bool libusbip::net::set_keepalive(SOCKET sockfd)
{
	if (auto timeout = get_keepalive_timeout()) { // windows tries 10 times every keepaliveinterval

		tcp_keepalive r {
			.onoff = 1,
			.keepalivetime = timeout*1000/2,
			.keepaliveinterval = timeout*1000/10/2,
		};
		
		DWORD outlen;
		auto err = WSAIoctl(sockfd, SIO_KEEPALIVE_VALS, &r, sizeof(r), nullptr, 0, &outlen, nullptr, nullptr);
		if (err) {
			spdlog::error("WSAIoctl(SIO_KEEPALIVE_VALS): WSAGetLastError {}", WSAGetLastError());
		}

		return !err;
	}

	int val = 1;

	auto err = setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&val, sizeof(val));
	if (err) {
		spdlog::error("setsockopt(SO_KEEPALIVE): WSAGetLastError {}", WSAGetLastError());
	}

	return !err;
}

auto libusbip::net::tcp_connect(const char *hostname, const char *port) -> Socket
{
	Socket sock;

	addrinfo hints{ .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	std::unique_ptr<addrinfo, decltype(freeaddrinfo)&> info(nullptr, freeaddrinfo);

	if (addrinfo *result; auto err = getaddrinfo(hostname, port, &hints, &result)) {
		spdlog::error("getaddrinfo(host='{}', port='{}'): {}", hostname, port, gai_strerrorA(err));
		return sock;
	} else {
		info.reset(result);
	}

	for (auto r = info.get(); r; r = r->ai_next) {

                sock.reset(socket(r->ai_family, r->ai_socktype, r->ai_protocol));
                if (!sock) {
                        continue;
                }

		if (auto h = sock.get(); !(set_nodelay(h) && set_keepalive(h))) {
			sock.close();
			break;
		}

		if (connect(sock.get(), r->ai_addr, int(r->ai_addrlen))) {
			spdlog::error("connect: WSAGetLastError {}", WSAGetLastError());
			sock.close();
		} else {
			break;
		}
	}

	return sock;
}
