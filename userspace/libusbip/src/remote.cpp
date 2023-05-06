/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn
 */

#include "..\remote.h"

#include "device_speed.h"
#include "op_common.h"
#include "last_error.h"
#include "strconv.h"
#include "output.h"

#include <usbip\proto_op.h>

#include <ws2tcpip.h>
#include <mstcpip.h>

namespace
{

using namespace usbip;

inline auto do_setsockopt(_In_ SOCKET s, _In_ int level, _In_ int optname, _In_ int optval)
{
	auto err = setsockopt(s, level, optname, reinterpret_cast<const char*>(&optval), sizeof(optval));
	if (err) {
		wsa_set_last_error wsa;
		libusbip::output("setsockopt(level={}, optname={}, optval={}) error {:#x}", 
			         level, optname, optval, wsa.error);	
	}

	return !err;
}

inline auto set_nodelay(_In_ SOCKET s)
{
	return do_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, true);
}

inline auto set_keepalive(_In_ SOCKET s)
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
auto set_keepalive(_In_ SOCKET s, _In_ ULONG timeout, _In_ ULONG interval)
{
	tcp_keepalive r {
		.onoff = true,
		.keepalivetime = timeout, // timeout(ms) with no activity until the first keep-alive packet is sent
		.keepaliveinterval = interval // interval(ms), between when successive keep-alive packets are sent if no acknowledgement is received
	};

	DWORD outlen;

	auto err = WSAIoctl(s, SIO_KEEPALIVE_VALS, &r, sizeof(r), nullptr, 0, &outlen, nullptr, nullptr);
	if (err) {
		wsa_set_last_error wsa;
		libusbip::output("WSAIoctl(SIO_KEEPALIVE_VALS) error {:#x}", wsa.error);
	}
	return !err;
}

auto recv(_In_ SOCKET s, _In_ void *buf, _In_ size_t len, _Out_opt_ bool *eof = nullptr)
{
	assert(s != INVALID_SOCKET);

	if (eof) {
		*eof = false;
	}

	switch (auto ret = ::recv(s, static_cast<char*>(buf), static_cast<int>(len), MSG_WAITALL)) {
	case SOCKET_ERROR:
		if (wsa_set_last_error wsa; wsa) {
			libusbip::output("recv error {:#x}", wsa.error);
		}
		return false;
	case 0: // connection has been gracefully closed
		if (len) {
			libusbip::output("recv EOF");
			if (eof) {
				*eof = true;
			}
		}
		[[fallthrough]];
	default:
		return ret == len;
	}
}

auto send(_In_ SOCKET s, _In_ const void *buf, _In_ size_t len)
{
	assert(s != INVALID_SOCKET);
	auto addr = static_cast<const char*>(buf);

	while (len) {
		auto ret = ::send(s, addr, static_cast<int>(len), 0);

		if (ret == SOCKET_ERROR) {
			wsa_set_last_error wsa;
			libusbip::output("send error {:#x}", wsa.error);
			return false;
		}

		addr += ret;
		len -= ret;
	}

	return true;
}

auto send_op_common(_In_ SOCKET s, _In_ uint16_t code)
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

auto recv_op_common(_In_ SOCKET s, _In_ uint16_t expected_code)
{
	assert(s != INVALID_SOCKET);

	op_common r{};
	if (recv(s, &r, sizeof(r))) {
		PACK_OP_COMMON(false, &r);
	} else {
		return GetLastError();
	}

	if (r.version != USBIP_VERSION) {
		return USBIP_ERROR_VERSION;
	}

	if (r.code != expected_code) {
		return USBIP_ERROR_PROTOCOL;
	}

	return op_status_error(static_cast<op_status_t>(r.status));
}

auto as_usb_device(_In_ const usbip_usb_device &d)
{
	return usb_device {
		.path = d.path,
		.busid = d.busid,

		.busnum = d.busnum,
		.devnum = d.devnum,
		.speed = win_speed(static_cast<usb_device_speed>(d.speed)),

		.idVendor = d.idVendor,
		.idProduct = d.idProduct,
		.bcdDevice = d.bcdDevice,

		.bDeviceClass = d.bDeviceClass,
		.bDeviceSubClass = d.bDeviceSubClass,
		.bDeviceProtocol = d.bDeviceProtocol,

		.bConfigurationValue = d.bConfigurationValue,

		.bNumConfigurations = d.bNumConfigurations,
		.bNumInterfaces = d.bNumInterfaces,
	};
}

} // namespace


const char* usbip::get_tcp_port() noexcept
{
	return tcp_port;
}

auto usbip::connect(_In_ const char *hostname, _In_ const char *service) -> Socket
{
	Socket sock;

	addrinfo hints{ .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	std::unique_ptr<addrinfo, decltype(freeaddrinfo)&> info(nullptr, freeaddrinfo);

	if (addrinfo *result; getaddrinfo(hostname, service, &hints, &result)) {
		wsa_set_last_error wsa; // see gai_strerror()
		libusbip::output("getaddrinfo {}:{} error {:#x}", hostname, service, wsa.error);
		return sock;
	} else {
		info.reset(result);
	}

	for (auto r = info.get(); r; r = r->ai_next) {

		sock.reset(socket(r->ai_family, r->ai_socktype, r->ai_protocol));
		if (!sock) {
			wsa_set_last_error wsa;
			libusbip::output("socket() {}:{} error {:#x}", hostname, service, wsa.error);
			continue;
		}

		using namespace std::chrono_literals;
		enum { 
		        timeout = std::chrono::milliseconds(30s).count(),
			interval = std::chrono::milliseconds(1s).count(),
		};

		if (auto h = sock.get(); !(set_nodelay(h) && set_keepalive(h, timeout, interval))) {
			set_last_error save; // close() can change last error
			sock.close();
			break;
		}

		if (connect(sock.get(), r->ai_addr, int(r->ai_addrlen))) {
			wsa_set_last_error wsa;
			libusbip::output("connect {}:{} error {:#x}", hostname, service, wsa.error);
			sock.close();
		} else {
			break;
		}
	}

	return sock;
}

bool usbip::enum_exportable_devices(
	_In_ SOCKET s, 
	_In_ const usb_device_f &on_dev, 
	_In_ const usb_interface_f &on_intf,
	_In_opt_ const usb_device_cnt_f &on_dev_cnt)
{
	assert(s != INVALID_SOCKET);
	
	if (!send_op_common(s, OP_REQ_DEVLIST)) {
		return false;
	}

	if (auto err = recv_op_common(s, OP_REP_DEVLIST)) {
		SetLastError(err);
		return false;
	}

	op_devlist_reply reply{};
	
	if (recv(s, &reply, sizeof(reply))) {
		PACK_OP_DEVLIST_REPLY(false, &reply);
	} else {
		return false;
	}

	libusbip::output("{} exportable device(s)", reply.ndev);
	assert(reply.ndev <= INT_MAX);

	if (on_dev_cnt) {
		on_dev_cnt(reply.ndev);
	}

	usb_device lib_dev;

	for (UINT32 i = 0; i < reply.ndev; ++i) {

		usbip_usb_device dev{};

		if (recv(s, &dev, sizeof(dev))) {
			usbip_net_pack_usb_device(false, &dev);
			lib_dev = as_usb_device(dev);
			on_dev(i, lib_dev);
		} else {
			return false;
		}

		for (int j = 0; j < dev.bNumInterfaces; ++j) {

			usbip_usb_interface intf{};

			if (recv(s, &intf, sizeof(intf))) {
				usbip_net_pack_usb_interface(false, &intf);
				static_assert(sizeof(intf) == sizeof(usb_interface));
				on_intf(i, lib_dev, j, reinterpret_cast<usb_interface&>(intf));
			} else {
				return false;
			}
		}
	}

	return true;
}
