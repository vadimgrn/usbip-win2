/*
* Copyright (C) 2021-2023 Vadym Hrynchyshyn
* Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "network.h"
#include "dbgcode.h"
#include "strconv.h"

#include <usbip\proto_op.h>

#include <ws2tcpip.h>
#include <mstcpip.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

auto do_setsockopt(SOCKET s, int level, int optname, int optval)
{
	auto err = setsockopt(s, level, optname, reinterpret_cast<const char*>(&optval), sizeof(optval));
	if (err) {
		auto ec = WSAGetLastError();
		spdlog::error("setsockopt(level={}, optname={}, optval={}) error {:#x} {}", 
			       level, optname, optval, ec, format_message(ec));
	}

	return !err;
}

inline auto set_nodelay(SOCKET s)
{
	return do_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, true);
}

inline auto set_keepalive(SOCKET s)
{
	return do_setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, true);
}

auto get_keepalive_timeout()
{
	auto &name = "KEEPALIVE_TIMEOUT";
	unsigned int value{};

	char buf[32];
	size_t required;

	if (auto err = getenv_s(&required, buf, sizeof(buf), name); !err) {
		sscanf_s(buf, "%u", &value);
	} else if (required) {
		spdlog::error("{} required buffer size is {}, error {:#x}", name, required, err);
	}

	return value;
}

auto set_keepalive_env(SOCKET s)
{
	auto val = get_keepalive_timeout();
	if (!val) {
		return set_keepalive(s);
	}

	tcp_keepalive r { // windows tries 10 times every keepaliveinterval
		.onoff = 1,
		.keepalivetime = val*1000/2,
		.keepaliveinterval = val*1000/(10*2)
	};

	DWORD outlen;

	auto err = WSAIoctl(s, SIO_KEEPALIVE_VALS, &r, sizeof(r), nullptr, 0, &outlen, nullptr, nullptr);
	if (err) {
		auto ec = WSAGetLastError();
		spdlog::error("WSAIoctl(SIO_KEEPALIVE_VALS) error {:#x} {}", ec, format_message(ec));
	}

	return !err;
}

} // namespace


bool usbip::net::recv(SOCKET s, void *buf, size_t len, bool *eof)
{
	assert(s != INVALID_SOCKET);

	switch (auto ret = ::recv(s, static_cast<char*>(buf), static_cast<int>(len), MSG_WAITALL)) {
	case SOCKET_ERROR:
		if (auto err = WSAGetLastError()) {
			spdlog::error("recv error {:#x} {}", err, format_message(err));
		}
		return false;
	case 0: // connection has been gracefully closed
		if (len) {
			spdlog::debug("recv: EOF");
			if (eof) {
				*eof = true;
			}
		}
		[[fallthrough]];
	default:
		return ret == len;
	}
}

bool usbip::net::send(SOCKET s, const void *buf, size_t len)
{
	assert(s != INVALID_SOCKET);
	auto addr = static_cast<const char*>(buf);

	while (len) {
		auto ret = ::send(s, addr, static_cast<int>(len), 0);

		if (ret == SOCKET_ERROR) {
			auto err = WSAGetLastError();
			spdlog::error("send error {:#x} {}", err, format_message(err));
			return false;
		}

		addr += ret;
		len -= ret;
	}

	return true;
}

bool usbip::net::send_op_common(SOCKET s, uint16_t code)
{
	assert(s != INVALID_SOCKET);

	op_common r {
		.version = USBIP_VERSION,
		.code = code,
		.status = ST_OK
	};

	PACK_OP_COMMON(true, &r);
	return send(s, &r, sizeof(r));
}

/*
 * @return err_t or op_status_t 
 * @see drivers/ude/network.cpp, recv_op_common.
 */
int usbip::net::recv_op_common(SOCKET s, uint16_t expected_code)
{
	assert(s != INVALID_SOCKET);
	
	op_common r;
	if (recv(s, &r, sizeof(r))) {
		PACK_OP_COMMON(false, &r);
	} else {
		return ERR_NETWORK;
	}

	if (r.version != USBIP_VERSION) {
		spdlog::error("op_common.version {} != {}", r.version, USBIP_VERSION);
		return ERR_VERSION;
	}

	if (r.code != expected_code) {
		spdlog::error("op_common.code {} != {}", r.code, expected_code);
		return ERR_PROTOCOL;
	}

	auto st = static_cast<op_status_t>(r.status);
	if (st) {
		spdlog::error("op_common.status #{} {}", st, op_status_str(st));
	}
	return st;
}

auto usbip::net::tcp_connect(const char *hostname, const char *service) -> Socket
{
	Socket sock;

	addrinfo hints{ .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	std::unique_ptr<addrinfo, decltype(freeaddrinfo)&> info(nullptr, freeaddrinfo);

	if (addrinfo *result; auto err = getaddrinfo(hostname, service, &hints, &result)) {
		auto msg = gai_strerror(err);
		spdlog::error("getaddrinfo {}:{} error {:#x} {}", hostname, service, err, wchar_to_utf8(msg));
		return sock;
	} else {
		info.reset(result);
	}

	for (auto r = info.get(); r; r = r->ai_next) {

                sock.reset(socket(r->ai_family, r->ai_socktype, r->ai_protocol));
                if (!sock) {
                        continue;
                }

		if (auto h = sock.get(); !(set_nodelay(h) && set_keepalive_env(h))) {
			sock.close();
			break;
		}

		if (connect(sock.get(), r->ai_addr, int(r->ai_addrlen))) {
			auto err = WSAGetLastError();
			spdlog::error("connect {}:{} error {:#x} {}", hostname, service, err, format_message(err));
			sock.close();
		} else {
			break;
		}
	}

	return sock;
}
