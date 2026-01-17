/*
 * Copyright (c) 2021-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "..\remote.h"
#include "..\win_handle.h"

#include "device_speed.h"
#include "op_common.h"
#include "last_error.h"
#include "strconv.h"
#include "output.h"

#include <usbip\proto_op.h>

#include <chrono>

#include <ws2tcpip.h>
#include <mstcpip.h>
#include <mswsock.h>

namespace
{

using namespace usbip;

/*
 * @see inet_ntop 
 */
auto address_to_string(_In_ const SOCKADDR &addr, _In_ DWORD len, _In_opt_ WSAPROTOCOL_INFO *info = nullptr)
{
	DWORD cch = 64; // for an IPv6 address, this buffer should be large enough to hold at least 46 characters
	std::wstring s(cch, L'\0');
	
	if (WSAAddressToString(const_cast<SOCKADDR*>(&addr), len, info, s.data(), &cch)) {
		auto err = WSAGetLastError();
		libusbip::output("WSAAddressToString error {}", err);
		cch = 0;
	} else {
		--cch; // exclude NULL terminator
	}

	s.resize(cch);
	return s;
}

auto do_setsockopt(_Inout_ set_last_error &last, _In_ SOCKET s, _In_ int level, _In_ int optname, _In_ int optval)
{
	auto err = setsockopt(s, level, optname, reinterpret_cast<const char*>(&optval), sizeof(optval));
	if (err) {
		last.error = WSAGetLastError();
		libusbip::output("setsockopt(level={}, optname={}, optval={}) error {}", 
			          level, optname, optval, last.error);	
	}
	return !err;
}

inline auto set_nodelay(_Inout_ set_last_error &last, _In_ SOCKET s)
{
	return do_setsockopt(last, s, IPPROTO_TCP, TCP_NODELAY, true);
}

inline auto set_keepalive(_Inout_ set_last_error &last, _In_ SOCKET s)
{
	return do_setsockopt(last, s, SOL_SOCKET, SO_KEEPALIVE, true);
}

inline auto set_ipv6only(_Inout_ set_last_error &last, _In_ SOCKET s, _In_ bool ipv6only)
{
	return do_setsockopt(last, s, IPPROTO_IPV6, IPV6_V6ONLY, ipv6only);
}

auto set_nonblock(_Inout_ set_last_error &last, _In_ SOCKET s, _In_ bool nonblock)
{
	u_long mode = nonblock;

	auto err = ioctlsocket(s, FIONBIO, &mode);
	if (err) {
		last.error = WSAGetLastError();
		libusbip::output("ioctlsocket(FIONBIO={}) error {}", nonblock, last.error);
	}
	return !err;
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
auto set_keepalive(_Inout_ set_last_error &last, _In_ SOCKET s, _In_ ULONG timeout, _In_ ULONG interval)
{
	tcp_keepalive r {
		.onoff = true,
		.keepalivetime = timeout, // timeout(ms) with no activity until the first keep-alive packet is sent
		.keepaliveinterval = interval // interval(ms), between when successive keep-alive packets are sent if no acknowledgement is received
	};

	DWORD outlen{};

	auto err = WSAIoctl(s, SIO_KEEPALIVE_VALS, &r, sizeof(r), nullptr, 0, &outlen, nullptr, nullptr);
	if (err) {
		last.error = WSAGetLastError();
		libusbip::output("WSAIoctl(SIO_KEEPALIVE_VALS) error {}", last.error);
	}
	return !err;
}

auto set_options(_Inout_ set_last_error &last, _In_ SOCKET s)
{
	using namespace std::chrono_literals;
	enum { 
		timeout = std::chrono::milliseconds(30s).count(),
		interval = std::chrono::milliseconds(1s).count(),
	};

	return  set_keepalive(last, s, timeout, interval) &&
		set_nodelay(last, s);
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
			libusbip::output("recv error {}", wsa.error);
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
		return ret == static_cast<int>(len);
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
			libusbip::output("send error {}", wsa.error);
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

	byteswap(r);
	return send(s, &r, sizeof(r));
}

auto recv_op_common(_In_ SOCKET s, _In_ uint16_t expected_code)
{
	assert(s != INVALID_SOCKET);

	op_common r{};
	if (recv(s, &r, sizeof(r))) {
		byteswap(r);
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

/*
 * WSAEnumNetworkEvents is not used because SOCKET is new,
 * WSAEventSelect here is the first call for it.
 */
auto prepare_event(_Inout_ set_last_error &last, _In_ SOCKET s, _In_ WSAEVENT evt)
{
	if (!WSAResetEvent(evt)) { // is reused in the loop
		last.error = WSAGetLastError();
		libusbip::output("WSAResetEvent error {}", last.error);
		return false;
	}

	if (WSAEventSelect(s, evt, FD_CONNECT)) { // sets socket to nonblocking mode
		last.error = WSAGetLastError();
		libusbip::output("WSAEventSelect(FD_CONNECT) error {}", last.error);
		return false;
	}

	return true;
}

auto try_connect(_In_ SOCKET s, _In_ WSAEVENT evt, _In_ const sockaddr &addr, _In_ DWORD len)
{
	libusbip::output(L"connecting to {}", address_to_string(addr, len));

	if (auto err = connect(s, &addr, len) ? WSAGetLastError() : 0; !err) {
		return 0;
	} else if (err != WSAEWOULDBLOCK) {
		libusbip::output("connect error {}", err);
		return err;
	}

	int err;

	switch (auto ret = WSAWaitForMultipleEvents(1, &evt, false, WSA_INFINITE, true)) {
	case WSA_WAIT_EVENT_0:
		if (WSANETWORKEVENTS events; WSAEnumNetworkEvents(s, evt, &events)) { // resets event if success
			err = WSAGetLastError();
			libusbip::output("WSAEnumNetworkEvents error {}", err);
		} else {
			assert(events.lNetworkEvents & FD_CONNECT);
			err = events.iErrorCode[FD_CONNECT_BIT];
		}
		break;
	case WSA_WAIT_IO_COMPLETION: // see QueueUserAPC
		libusbip::output("connect cancelled");
		err = ERROR_CANCELLED;
		break;
	default:
		assert(ret == WSA_WAIT_FAILED);

		err = WSAGetLastError();
		assert(err != ERROR_CANCELLED);

		libusbip::output("WSAWaitForMultipleEvents -> {}, error {}", ret, err);
	}

	return err;
}

INT wait_for_resolve(_Inout_ OVERLAPPED &ovlp, _In_opt_ HANDLE cancel, _In_ bool alertable)
{
	INT err;

	switch (auto ret = WaitForSingleObjectEx(ovlp.hEvent, INFINITE, alertable)) {
	case WAIT_OBJECT_0:
		if (err = GetAddrInfoExOverlappedResult(&ovlp); err) {
			libusbip::output("GetAddrInfoExOverlappedResult error {}", err);
		}
		break;
	case WAIT_IO_COMPLETION: // see QueueUserAPC
		libusbip::output("GetAddrInfoEx cancelled by APC");
		if (err = GetAddrInfoExCancel(&cancel); err) {
			libusbip::output("GetAddrInfoExOverlappedResult error {}", err);
		} else {
			err = wait_for_resolve(ovlp, HANDLE(), false); // see WSA_E_CANCELLED
		}
		break;
	default:
		assert(ret == WAIT_FAILED);
		err = GetLastError();
		libusbip::output("WaitForSingleObjectEx(alertable={}) -> {}, error {}", alertable, ret, err);
	}

	return err;
}

/*
 * Numeric IP addresses like "XXX.XXX.XXX.XXX" are resolved instantly. 
 */
auto resolve(_Inout_ set_last_error &last, _In_ const char *hostname, _In_ const char *service)
{
	std::unique_ptr<ADDRINFOEX, decltype(FreeAddrInfoEx)&> ptr(nullptr, FreeAddrInfoEx);

	NullableHandle evt(CreateEvent(nullptr, true, false, nullptr));
	if (!evt) {
		last.error = WSAGetLastError();
		libusbip::output("CreateEvent error {}", last.error);
		return ptr;
	}

	OVERLAPPED ovlp { .hEvent = evt.get() };

	auto host = utf8_to_wchar(hostname);
	auto svc = utf8_to_wchar(service);

	const ADDRINFOEX hints{ .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	ADDRINFOEX *result{};
	HANDLE cancel{};

	libusbip::output("resolving {}:{}", hostname, service);

	last.error = GetAddrInfoEx(host.c_str(), svc.c_str(), NS_ALL, nullptr, 
				   &hints, &result, nullptr, &ovlp, nullptr, &cancel);

	switch (last.error) {
	case WSA_IO_PENDING:
		if (last.error = wait_for_resolve(ovlp, cancel, true); last.error) {
			break;
		}
		[[fallthrough]];
	case NO_ERROR:
		ptr.reset(result);
		break;
	default:
		libusbip::output("GetAddrInfoEx error {}", last.error);
	}

	return ptr;
}

inline auto connect_by_name(_In_ SOCKET s, _In_ LPCWSTR hostname, _In_ _In_ LPCWSTR service) noexcept
{
	return WSAConnectByName(s, const_cast<wchar_t*>(hostname), const_cast<wchar_t*>(service), 
				nullptr, nullptr, // LocalAddress
				nullptr, nullptr, // RemoteAddress
				nullptr, nullptr);
}

} // namespace


const char* usbip::get_tcp_port() noexcept
{
	return tcp_port;
}

/*
 * QueueUserAPC() does not terminate connect/WSAConnectByName.
 */
auto usbip::connect(_In_ const char *hostname, _In_ const char *service) -> Socket
{
	auto host = utf8_to_wchar(hostname);
	auto svc = utf8_to_wchar(service);

	set_last_error last(NO_ERROR); // restore after sock.close()
	Socket sock;

	for (auto family: {AF_INET, AF_INET6}) {

		sock.reset(socket(family, SOCK_STREAM, 0));

		if (!sock) {
			last.error = WSAGetLastError();
			libusbip::output("socket(family={}) error {}", family, last.error);
		} else if (family == AF_INET6 && !set_ipv6only(last, sock.get(), false)) {
			//
		} else if (!set_options(last, sock.get())) {
			//
		} else if (!connect_by_name(sock.get(), host.c_str(), svc.c_str())) {
			last.error = WSAGetLastError();
			libusbip::output("WSAConnectByName(family={}) error {}", family, last.error);
			break; // it makes no sense to try next family
		} else if (setsockopt(sock.get(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0)) {
			last.error = WSAGetLastError();
			libusbip::output("setsockopt(SO_UPDATE_CONNECT_CONTEXT) error {}", last.error);
			break;
		} else {
			return sock;
		}
	}

	sock.close();
	return sock;
}

auto usbip::connect(_In_ const char *hostname, _In_ const char *service, _In_ unsigned long options) -> Socket
{
	set_last_error last(ERROR_INVALID_PARAMETER); // restore after sock.close()
	Socket sock;

	if (options != CANCEL_BY_APC) {
		return sock;
	}

	auto ai = resolve(last, hostname, service);
	if (!ai) {
		return sock;
	}

	WSAEvent evt(WSACreateEvent());
	if (!evt) {
		last.error = WSAGetLastError();
		libusbip::output("WSACreateEvent error {}", last.error);
		return sock;
	}

	for (auto r = ai.get(); r; r = r->ai_next) {

		sock.reset(socket(r->ai_family, r->ai_socktype, r->ai_protocol));

		if (!sock) {
			last.error = WSAGetLastError();
			libusbip::output("socket(family={}) error {}", r->ai_family, last.error);
		} else if (auto ok = set_options(last, sock.get()) && prepare_event(last, sock.get(), evt.get()); !ok) {
			//
		} else if (auto err = try_connect(sock.get(), evt.get(), *r->ai_addr, static_cast<DWORD>(r->ai_addrlen))) {
			if (last.error = err; err == ERROR_CANCELLED) {
				break;
			}
		} else if (WSAEventSelect(sock.get(), WSA_INVALID_EVENT, 0)) { // cancel the association and selection of network events
			last.error = WSAGetLastError();
			libusbip::output("WSAEventSelect(0) error {}", last.error);
		} else if (set_nonblock(last, sock.get(), false)) {
			return sock;
		}
	}

	sock.close();
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
		byteswap(reply);
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

		op_devlist_reply_extra extra{};

		if (recv(s, &extra, sizeof(extra))) {
			byteswap(extra);
			lib_dev = as_usb_device(extra.udev);
			on_dev(i, lib_dev);
		} else {
			return false;
		}

		for (int j = 0; j < lib_dev.bNumInterfaces; ++j) {

			usbip_usb_interface intf{};

			if (recv(s, &intf, sizeof(intf))) {
				byteswap(intf);
				static_assert(sizeof(intf) == sizeof(usb_interface));
				on_intf(i, lib_dev, j, reinterpret_cast<usb_interface&>(intf));
			} else {
				return false;
			}
		}
	}

	return true;
}
