/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn
 */

#include "remote.h"
#include "strconv.h"

#include <usbip\proto_op.h>
#include <libusbip\op_common.h>

#include <ws2tcpip.h>
#include <mstcpip.h>

#include <memory>
#include <chrono>

namespace
{

using namespace usbip;

inline auto do_setsockopt(SOCKET s, int level, int optname, int optval)
{
	return !setsockopt(s, level, optname, reinterpret_cast<const char*>(&optval), sizeof(optval));
}

inline auto set_nodelay(SOCKET s)
{
	return do_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, true);
}

inline auto set_keepalive(SOCKET s)
{
	return do_setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, true);
}

/*
 * The default system-wide value of the keep-alive timeout is controllable 
 * through the KeepAliveTime registry setting which takes a value in milliseconds. 
 * If the key is not set, the default keep-alive timeout is 2 hours. 
 *
 * The default system-wide value of the keep-alive interval is controllable through 
 * the KeepAliveInterval registry setting which takes a value in milliseconds. 
 * If the key is not set, the default keep-alive interval is 1 second.
 * 
 * On Windows Vista and later, the number of keep-alive probes (data retransmissions) 
 * is set to 10 and cannot be changed. 
 */
auto set_keepalive(SOCKET s, ULONG timeout, ULONG interval)
{
	tcp_keepalive r {
		.onoff = true,
		.keepalivetime = timeout, // timeout(ms) with no activity until the first keep-alive packet is sent
		.keepaliveinterval = interval // interval(ms), between when successive keep-alive packets are sent if no acknowledgement is received
	};

	DWORD outlen;
	return !WSAIoctl(s, SIO_KEEPALIVE_VALS, &r, sizeof(r), nullptr, 0, &outlen, nullptr, nullptr);
}

auto recv(SOCKET s, void *buf, size_t len, bool *eof = nullptr)
{
	assert(s != INVALID_SOCKET);

	switch (auto ret = ::recv(s, static_cast<char*>(buf), static_cast<int>(len), MSG_WAITALL)) {
	case SOCKET_ERROR:
		SetLastError(WSAGetLastError());
		return false;
	case 0: // connection has been gracefully closed
		if (len && eof) {
			*eof = true;
		}
		[[fallthrough]];
	default:
		return ret == len;
	}
}

auto send(SOCKET s, const void *buf, size_t len)
{
	assert(s != INVALID_SOCKET);
	auto addr = static_cast<const char*>(buf);

	while (len) {
		auto ret = ::send(s, addr, static_cast<int>(len), 0);

		if (ret == SOCKET_ERROR) {
			SetLastError(WSAGetLastError());
			return false;
		}

		addr += ret;
		len -= ret;
	}

	return true;
}

auto send_op_common(SOCKET s, uint16_t code)
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

auto recv_op_common(SOCKET s, uint16_t expected_code)
{
	assert(s != INVALID_SOCKET);

	op_common r;
	if (recv(s, &r, sizeof(r))) {
		PACK_OP_COMMON(false, &r);
	} else {
		UINT32 err = GetLastError();
		return err;
	}

	if (r.version != USBIP_VERSION) {
		return ERROR_USBIP_VERSION;
	}

	if (r.code != expected_code) {
		return ERROR_USBIP_PROTOCOL;
	}

	auto st = static_cast<op_status_t>(r.status);
	return op_status_error(st);
}

} // namespace


auto usbip::connect(const char *hostname, const char *service) -> Socket
{
	Socket sock;

	addrinfo hints{ .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	std::unique_ptr<addrinfo, decltype(freeaddrinfo)&> info(nullptr, freeaddrinfo);

	if (addrinfo *result; getaddrinfo(hostname, service, &hints, &result)) {
		SetLastError(WSAGetLastError()); // see gai_strerror()
		return sock;
	} else {
		info.reset(result);
	}

	for (auto r = info.get(); r; r = r->ai_next) {

		sock.reset(socket(r->ai_family, r->ai_socktype, r->ai_protocol));
		if (!sock) {
			SetLastError(WSAGetLastError());
			continue;
		}

		using namespace std::chrono_literals;
		enum { 
		        timeout = std::chrono::milliseconds(30s).count(),
			interval = std::chrono::milliseconds(1s).count(),
		};

		if (auto h = sock.get(); !(set_nodelay(h) && set_keepalive(h, timeout, interval))) {
			auto saved = WSAGetLastError();
			sock.close();
			SetLastError(saved);
			break;
		}

		if (connect(sock.get(), r->ai_addr, int(r->ai_addrlen))) {
			auto saved = WSAGetLastError();
			sock.close();
			SetLastError(saved);
		} else {
			break;
		}
	}

	return sock;
}

/*
 * @return call GetLastError() if false is returned
 */
bool usbip::enum_exportable_devices(
	SOCKET s, 
	const usbip_usb_device_f &on_dev, 
	const usbip_usb_interface_f &on_intf,
	const usbip_usb_device_cnt_f &on_dev_cnt)
{
	assert(s != INVALID_SOCKET);
	
	if (!send_op_common(s, OP_REQ_DEVLIST)) {
		return false;
	}

	if (auto err = recv_op_common(s, OP_REP_DEVLIST)) {
		SetLastError(err);
		return false;
	}

	op_devlist_reply reply;
	
	if (recv(s, &reply, sizeof(reply))) {
		PACK_OP_DEVLIST_REPLY(false, &reply);
	} else {
		return false;
	}

	assert(reply.ndev <= INT_MAX);

	if (on_dev_cnt) {
		on_dev_cnt(reply.ndev);
	}

	for (UINT32 i = 0; i < reply.ndev; ++i) {

		usbip_usb_device dev;

		if (recv(s, &dev, sizeof(dev))) {
			usbip_net_pack_usb_device(false, &dev);
			on_dev(i, dev);
		} else {
			return false;
		}

		for (int j = 0; j < dev.bNumInterfaces; ++j) {

			usbip_usb_interface intf;

			if (recv(s, &intf, sizeof(intf))) {
				usbip_net_pack_usb_interface(false, &intf);
				on_intf(i, dev, j, intf);
			} else {
				return false;
			}
		}
	}

	return true;
}
