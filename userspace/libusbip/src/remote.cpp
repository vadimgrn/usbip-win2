/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../remote.h"
#include "../win_handle.h"

#include "last_error.h"
#include "device_speed.h"
#include "op_common.h"
#include "strconv.h"
#include "output.h"

#include <usbip/proto_op.h>
#include <chrono>
#include <expected>

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

int do_setsockopt(_In_ SOCKET s, _In_ int level, _In_ int optname, _In_ int optval)
{
	if (setsockopt(s, level, optname, reinterpret_cast<const char*>(&optval), sizeof(optval))) {
		auto err = WSAGetLastError();
		libusbip::output("setsockopt(level={}, optname={}, optval={}) error {}", 
			          level, optname, optval, err);
		return err;
	}

        return 0;
}

inline int set_nodelay(_In_ SOCKET s)
{
	return do_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, true);
}

int set_nonblock(_In_ SOCKET s, _In_ bool nonblock)
{
	u_long mode = nonblock;

	if (ioctlsocket(s, FIONBIO, &mode)) {
		auto err = WSAGetLastError();
		libusbip::output("ioctlsocket(FIONBIO={}) error {}", nonblock, err);
		return err;
	}

        return 0;
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
int set_keepalive(_In_ SOCKET s, _In_ ULONG timeout, _In_ ULONG interval)
{
	tcp_keepalive r {
		.onoff = true,
		.keepalivetime = timeout, // timeout(ms) with no activity until the first keep-alive packet is sent
		.keepaliveinterval = interval // interval(ms), between when successive keep-alive packets are sent if no acknowledgement is received
	};

	DWORD outlen{};

	if (WSAIoctl(s, SIO_KEEPALIVE_VALS, &r, sizeof(r), nullptr, 0, &outlen, nullptr, nullptr)) {
		auto err = WSAGetLastError();
		libusbip::output("WSAIoctl(SIO_KEEPALIVE_VALS) error {}", err);
		return err;
	}
	return 0;
}

int set_options(_In_ SOCKET s)
{
	using namespace std::chrono_literals;
	enum { 
		timeout = std::chrono::milliseconds(30s).count(),
		interval = std::chrono::milliseconds(1s).count(),
	};

	if (auto err = set_keepalive(s, timeout, interval)) {
		return err;
	}
	return set_nodelay(s);
}

auto recv(_In_ SOCKET s, _In_ void *buf, _In_ size_t len)
{
	assert(s != INVALID_SOCKET);
	assert(len <= INT_MAX);

	auto ret = ::recv(s, static_cast<char*>(buf), static_cast<int>(len), MSG_WAITALL);

        if (ret == SOCKET_ERROR) {
		if (auto wsa = make_wsa_last_error(); wsa) {
			libusbip::output("recv error {}", wsa.error);
		}
		return false;
	}

	if (static_cast<size_t>(ret) == len) {
                return true;
        }

        set_last_error last(static_cast<DWORD>(WSAECONNRESET));

        if (ret) {
		libusbip::output("recv EOF: received {} of {} bytes", ret, len);
	} else {
		libusbip::output("recv EOF");
	}

        return false;
}

auto send(_In_ SOCKET s, _In_ const void *buf, _In_ size_t len)
{
	assert(s != INVALID_SOCKET);
	auto addr = static_cast<const char*>(buf);

	while (len) {
		auto ret = ::send(s, addr, static_cast<int>(len), 0);

		if (ret == SOCKET_ERROR) {
			auto wsa = make_wsa_last_error();
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

constexpr auto as_usb_device(_In_ const usbip_usb_device &d)
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

constexpr auto as_usb_interface(_In_ const usbip_usb_interface &r)
{
        static_assert(sizeof(r) == sizeof(usb_interface));
        return usb_interface {
                .bInterfaceClass = r.bInterfaceClass,
                .bInterfaceSubClass = r.bInterfaceSubClass,
                .bInterfaceProtocol = r.bInterfaceProtocol,
                .padding = r.padding,
        };
}

/*
 * WSAEnumNetworkEvents is not used because SOCKET is new,
 * WSAEventSelect here is the first call for it.
 */
int prepare_event(_In_ SOCKET s, _In_ WSAEVENT evt)
{
	if (!WSAResetEvent(evt)) { // is reused in the loop
		auto err = WSAGetLastError();
		libusbip::output("WSAResetEvent error {}", err);
		return err;
	}

	if (WSAEventSelect(s, evt, FD_CONNECT)) { // sets socket to nonblocking mode
		auto err = WSAGetLastError();
		libusbip::output("WSAEventSelect(FD_CONNECT) error {}", err);
		return err;
	}

	return 0;
}

auto check_cancelled(_In_opt_ HANDLE cancel_evt)
{
	if (cancel_evt && WaitForSingleObject(cancel_evt, 0) == WAIT_OBJECT_0) {
		libusbip::output("connect cancelled");
		return ERROR_CANCELLED;
	}

	return ERROR_SUCCESS;
}

int try_connect(_In_ SOCKET s, _In_ WSAEVENT evt, _In_opt_ HANDLE cancel_evt, _In_ const sockaddr &addr, _In_ DWORD len)
{
	if (auto err = check_cancelled(cancel_evt)) {
		return err;
	}

	libusbip::output(L"connecting to {}", address_to_string(addr, len));

	if (auto err = connect(s, &addr, len) ? WSAGetLastError() : 0; !err) {
		return check_cancelled(cancel_evt);
	} else if (err != WSAEWOULDBLOCK) {
		libusbip::output("connect error {}", err);
		return err;
	}

	WSAEVENT events[] { evt, cancel_evt };
	DWORD cnt = cancel_evt ? 2 : 1;
	
	for (int err;;) {
		switch (auto ret = WSAWaitForMultipleEvents(cnt, events, false, WSA_INFINITE, true)) {
		case WSA_WAIT_EVENT_0:
			if (WSANETWORKEVENTS net_events; WSAEnumNetworkEvents(s, evt, &net_events)) { // resets event if success
				err = WSAGetLastError();
				libusbip::output("WSAEnumNetworkEvents error {}", err);
			} else if (net_events.lNetworkEvents & FD_CONNECT) {
				err = net_events.iErrorCode[FD_CONNECT_BIT];
			} else {
				err = WSAECONNABORTED;
			}
			return err;
		case WSA_WAIT_EVENT_0 + 1:
			libusbip::output("connect cancelled");
			return ERROR_CANCELLED;
		case WSA_WAIT_IO_COMPLETION: // see QueueUserAPC
			continue;
		default:
			assert(ret == WSA_WAIT_FAILED);

			err = WSAGetLastError();
			assert(err != ERROR_CANCELLED);

			libusbip::output("WSAWaitForMultipleEvents -> {}, error {}", ret, err);
			return err;
		}
	}
}

int wait_for_resolve(_Inout_ OVERLAPPED &ovlp, _In_opt_ HANDLE cancel, _In_opt_ HANDLE cancel_evt)
{
	HANDLE events[] { ovlp.hEvent, cancel_evt };
	DWORD cnt = cancel_evt ? 2 : 1;
	
	for (INT err;;) {
		switch (auto ret = WaitForMultipleObjectsEx(cnt, events, false, INFINITE, true)) {
		case WAIT_OBJECT_0:
			if (err = GetAddrInfoExOverlappedResult(&ovlp); err) {
				libusbip::output("GetAddrInfoExOverlappedResult error {}", err);
			}
			return err;
		case WAIT_OBJECT_0 + 1:
			libusbip::output("GetAddrInfoEx cancelled");
			if (cancel) {
				if (auto cancel_err = GetAddrInfoExCancel(&cancel)) {
					libusbip::output("GetAddrInfoExCancel error {}", cancel_err);
				}
			}
			WaitForSingleObject(ovlp.hEvent, INFINITE);
			GetAddrInfoExOverlappedResult(&ovlp);
			return ERROR_CANCELLED;
		case WAIT_IO_COMPLETION: // see QueueUserAPC
			continue;
		default:
			assert(ret == WAIT_FAILED);
			err = GetLastError();
			libusbip::output("WaitForMultipleObjectsEx -> {}, error {}", ret, err);
			return err;
		}
	}
}

using addrinfo_ptr = std::unique_ptr<ADDRINFOEX, decltype(FreeAddrInfoEx)&>;

/*
 * Numeric IP addresses like "XXX.XXX.XXX.XXX" are resolved instantly. 
 */
auto resolve(_In_ const char *hostname, _In_ const char *service, _In_opt_ HANDLE cancel_evt)
	-> std::expected<addrinfo_ptr, DWORD>
{
	addrinfo_ptr ptr(nullptr, FreeAddrInfoEx);

	auto host = utf8_to_wchar(hostname);
	auto svc = utf8_to_wchar(service);

	if (!(host && svc)) {
		DWORD err = !host ? host.error() : svc.error();
		libusbip::output("utf8_to_wchar('{}','{}') error {}", hostname, service, err);
		return std::unexpected(err); 
	}

	NullableHandle evt(CreateEvent(nullptr, true, false, nullptr));
	if (!evt) {
		DWORD err = GetLastError();
		libusbip::output("CreateEvent error {}", err);
		return std::unexpected(err);
	}

	OVERLAPPED ovlp{};
	ovlp.hEvent = evt.get();

	ADDRINFOEX hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	ADDRINFOEX *result{};
	HANDLE cancel{};

	libusbip::output("resolving {}:{}", hostname, service);

	DWORD err = GetAddrInfoEx(host->c_str(), svc->c_str(), NS_ALL, nullptr, 
				  &hints, &result, nullptr, &ovlp, nullptr, &cancel);

	switch (err) {
	case WSA_IO_PENDING:
		err = wait_for_resolve(ovlp, cancel, cancel_evt);
		break;
	case NO_ERROR:
		break;
	default:
		libusbip::output("GetAddrInfoEx error {}", err);
	}

	ptr.reset(result);

	if (err) {
		ptr.reset();
		return std::unexpected(err);
	}

	return ptr;
}

auto do_connect(
        _In_ const char *hostname, _In_ const char *service, _In_opt_ HANDLE cancel_event)
        -> std::expected<Socket, DWORD>
{
	if (auto err = check_cancelled(cancel_event)) {
		return std::unexpected(static_cast<DWORD>(err));
	}

	auto ai = resolve(hostname, service, cancel_event);
	if (!ai) {
		return std::unexpected(ai.error());
	}

	WSAEvent evt(WSACreateEvent());
	if (!evt) {
		auto err = WSAGetLastError();
		libusbip::output("WSACreateEvent error {}", err);
		return std::unexpected(err);
	}

	DWORD last_err = ERROR_INVALID_PARAMETER;
	Socket sock;

	for (auto r = ai->get(); r; r = r->ai_next) {

		if (auto err = check_cancelled(cancel_event)) {
			last_err = err;
			break;
		}

		sock.reset(socket(r->ai_family, r->ai_socktype, r->ai_protocol));

		if (!sock) {
			last_err = WSAGetLastError();
			libusbip::output("socket(family={}) error {}", r->ai_family, last_err);
			continue;
		}

		if (auto err = set_options(sock.get())) {
			last_err = err;
			continue;
		}

		if (auto err = prepare_event(sock.get(), evt.get())) {
			last_err = err;
			continue;
		}

		if (auto err = try_connect(sock.get(), evt.get(), cancel_event, *r->ai_addr, static_cast<DWORD>(r->ai_addrlen))) {
			last_err = err;
			if (err == ERROR_CANCELLED) {
				break;
			}
			continue;
		}

		if (WSAEventSelect(sock.get(), WSA_INVALID_EVENT, 0)) { // cancel the association and selection of network events
			last_err = WSAGetLastError();
			libusbip::output("WSAEventSelect(0) error {}", last_err);
			continue;
		}

		if (auto err = set_nonblock(sock.get(), false)) {
			last_err = err;
			continue;
		}

		return sock;
	}

	sock.close();
	return std::unexpected(last_err);
}

} // namespace


const char* usbip::get_tcp_port() noexcept
{
	return tcp_port;
}

auto usbip::connect(
        _In_ const char *hostname, _In_ const char *service, _In_opt_ HANDLE cancel_event) -> Socket
{
	auto res = do_connect(hostname, service, cancel_event);
	if (!res) {
		SetLastError(res.error());
		return Socket{};
	}
	return std::move(*res);
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

	if (reply.ndev > INT_MAX) {
		libusbip::output("the number of exportable devices {} is too large", reply.ndev);
		SetLastError(ERROR_INVALID_DATA);
		return false;
	}

        libusbip::output("{} exportable device(s)", reply.ndev);

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

			if (usbip_usb_interface intf{}; recv(s, &intf, sizeof(intf))) {
				byteswap(intf);
				auto uintf = as_usb_interface(intf);
				on_intf(i, lib_dev, j, uintf);
			} else {
				return false;
			}
		}
	}

	return true;
}
