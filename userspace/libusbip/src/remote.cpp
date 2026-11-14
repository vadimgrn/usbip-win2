/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../remote.h"

#include "output.h"
#include "op_common.h"
#include "last_error.h"
#include "stop_event.h"
#include "device_speed.h"

#include <usbip/proto_op.h>

#include <ws2tcpip.h>
#include <mstcpip.h>

namespace
{

using namespace usbip;

struct resolve_state
{
        resolve_state(std::wstring h, std::wstring s) noexcept :
                host(std::move(h)),
                svc(std::move(s)) {}

        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(evt); }

        std::wstring host;
        std::wstring svc;

        ADDRINFOEX hints {
                .ai_family = AF_UNSPEC,
                .ai_socktype = SOCK_STREAM,
        };

        NullableHandle evt{ CreateEvent(nullptr, true, false, nullptr) };
        OVERLAPPED ovlp{ .hEvent = evt.get() };
        ADDRINFOEX *result{};
        HANDLE cancel{};
        bool abandoned{}; // pending GetAddrInfoEx has not completed after cancellation, see cancel_resolve
};

/*
 * @see inet_ntop
 */
auto address_to_string(_In_ const SOCKADDR &addr, _In_ int len, _In_opt_ WSAPROTOCOL_INFO *info = nullptr)
{
        DWORD cch = 64; // for an IPv6 address, this buffer should be large enough to hold at least 46 characters
        std::wstring s(cch, L'\0');
        auto dwlen = static_cast<DWORD>(len);

        auto res = WSAAddressToString(const_cast<SOCKADDR*>(&addr), dwlen, info, s.data(), &cch);
        if (res && WSAGetLastError() == WSAEFAULT && cch > 0) {
                s.resize(cch);
                res = WSAAddressToString(const_cast<SOCKADDR*>(&addr), dwlen, info, s.data(), &cch);
        }

        if (res) {
                auto err = WSAGetLastError();
                libusbip::output("WSAAddressToString error {}", err);
                cch = 0;
        } else {
                --cch; // exclude NULL terminator
        }

        s.resize(cch);
        return s;
}

DWORD do_setsockopt(_In_ SOCKET s, _In_ int level, _In_ int optname, _In_ int optval)
{
        if (setsockopt(s, level, optname, reinterpret_cast<const char*>(&optval), sizeof(optval))) {
                auto err = WSAGetLastError();
                libusbip::output("setsockopt(level={}, optname={}, optval={}) error {}",
                                  level, optname, optval, err);
                return err;
        }

        return ERROR_SUCCESS;
}

inline auto set_nodelay(_In_ SOCKET s)
{
        return do_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, true);
}

DWORD set_nonblock(_In_ SOCKET s, _In_ bool nonblock)
{
        if (u_long mode = nonblock; ioctlsocket(s, FIONBIO, &mode)) {
                auto err = WSAGetLastError();
                libusbip::output("ioctlsocket(FIONBIO={}) error {}", nonblock, err);
                return err;
        }

        return ERROR_SUCCESS;
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
DWORD set_keepalive(_In_ SOCKET s, _In_ ULONG timeout, _In_ ULONG interval)
{
        tcp_keepalive r {
                .onoff = true,
                .keepalivetime = timeout, // timeout(ms) with no activity until the first keep-alive packet is sent
                .keepaliveinterval = interval // interval(ms), between when successive keep-alive packets are sent if no acknowledgement is received
        };

        if (DWORD outlen;
            WSAIoctl(s, SIO_KEEPALIVE_VALS, &r, sizeof(r), nullptr, 0, &outlen, nullptr, nullptr)) {
                auto err = WSAGetLastError();
                libusbip::output("WSAIoctl(SIO_KEEPALIVE_VALS) error {}", err);
                return err;
        }

        return ERROR_SUCCESS;
}

auto set_options(_In_ SOCKET s)
{
        enum { second = 1000, timeout = 30*second, interval = 1*second }; // milliseconds

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
                auto wsa = make_wsa_last_error();
                libusbip::output("recv error {}", wsa.error);
                return false;
        }

        if (static_cast<size_t>(ret) == len) {
                return true;
        }

        set_last_error last(WSAECONNRESET);

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
        assert(len <= INT_MAX);
        auto addr = static_cast<const char*>(buf);

        while (len) {
                auto ret = ::send(s, addr, static_cast<int>(len), 0);

                if (ret == SOCKET_ERROR) {
                        auto wsa = make_wsa_last_error();
                        libusbip::output("send error {}", wsa.error);
                        return false;
                } else if (!ret) {
                        set_last_error last(WSAECONNRESET);
                        libusbip::output("send EOF");
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

        op_common r;
        if (recv(s, &r, sizeof(r))) {
                byteswap(r);
        } else {
                return GetLastError();
        }

        if (r.version != USBIP_VERSION) {
                libusbip::output("recv_op_common: invalid version {:#x}, expected {:#x}", r.version, static_cast<uint16_t>(USBIP_VERSION));
                return USBIP_ERROR_VERSION;
        }

        if (r.code != expected_code) {
                libusbip::output("recv_op_common: unexpected code {:#x}, expected {:#x}", r.code, expected_code);
                return USBIP_ERROR_PROTOCOL;
        }

        return op_status_error(static_cast<op_status_t>(r.status));
}

auto as_usb_device(_In_ const usbip_usb_device &d, _In_ USB_DEVICE_SPEED speed)
{
        return usb_device {
                .path{ to_string_view(d.path) },
                .busid{ to_string_view(d.busid) },

                .busnum = d.busnum,
                .devnum = d.devnum,
                .speed = speed,

                .idVendor = d.idVendor,
                .idProduct = d.idProduct,
                .bcdDevice = d.bcdDevice,

                .bDeviceClass = d.bDeviceClass,
                .bDeviceSubClass = d.bDeviceSubClass,
                .bDeviceProtocol = d.bDeviceProtocol,

                .bConfigurationValue = d.bConfigurationValue,
                .bNumConfigurations = d.bNumConfigurations,
        };
}

constexpr auto as_usb_interface(_In_ const usbip_usb_interface &r) noexcept
{
        return usb_interface {
                .bInterfaceClass = r.bInterfaceClass,
                .bInterfaceSubClass = r.bInterfaceSubClass,
                .bInterfaceProtocol = r.bInterfaceProtocol,
        };
}

/*
 * WSAEnumNetworkEvents is not used because SOCKET is new,
 * WSAEventSelect here is the first call for it.
 */
DWORD prepare_event(_In_ SOCKET s, _In_ WSAEVENT evt)
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

        return ERROR_SUCCESS;
}

DWORD get_connect_result(_In_ SOCKET s, _In_ WSAEVENT evt)
{
        WSANETWORKEVENTS events;
        if (WSAEnumNetworkEvents(s, evt, &events)) { // resets event if success
                auto err = WSAGetLastError();
                libusbip::output("WSAEnumNetworkEvents error {}", err);
                return err;
        }

        if (!(events.lNetworkEvents & FD_CONNECT)) {
                return WSAECONNABORTED;
        }

        auto st = events.iErrorCode[FD_CONNECT_BIT];
        if (st) {
                libusbip::output("FD_CONNECT error {}", st);
        }
        return st;
}

/*
 * An infinite wait (WSA_INFINITE) is safe here: the Windows TCP/IP stack enforces
 * its own connection timeout via SYN retransmissions (~21-42s), signaling FD_CONNECT
 * with WSAETIMEDOUT if unreachable. Cancellation via cancel_evt is also supported.
 */
DWORD wait_for_connect(_In_ SOCKET s, _In_ WSAEVENT evt, _In_opt_ HANDLE cancel_evt)
{
        WSAEVENT events[] { evt, cancel_evt };
        DWORD cnt = cancel_evt ? 2 : 1;

        auto on_cancelled = []
        {
                libusbip::output("connect cancelled");
                return ERROR_CANCELLED;
        };

        for (;;) {
                switch (auto ret = WSAWaitForMultipleEvents(cnt, events, false, WSA_INFINITE, true)) {
                case WSA_WAIT_EVENT_0:
                        return is_signaled(cancel_evt) ? on_cancelled() : get_connect_result(s, evt);
                case WSA_WAIT_EVENT_0 + 1:
                        return on_cancelled();
                case WSA_WAIT_IO_COMPLETION: // see QueueUserAPC
                        continue;
                default:
                        assert(ret == WSA_WAIT_FAILED);

                        auto err = WSAGetLastError();
                        assert(err != ERROR_CANCELLED);

                        libusbip::output("WSAWaitForMultipleEvents -> {}, error {}", ret, err);
                        return err;
                }
        }
}

DWORD try_connect(
        _In_ SOCKET s, _In_ WSAEVENT evt, _In_opt_ HANDLE cancel_evt, _In_ const sockaddr &addr, _In_ int len)
{
        if (auto err = check_cancelled(cancel_evt)) {
                libusbip::output("connect cancelled");
                return err;
        }

        if (libusbip::is_output_enabled()) {
                libusbip::output(L"connecting to {}", address_to_string(addr, len));
        }

        if (!::connect(s, &addr, len)) { // success
                if (auto err = check_cancelled(cancel_evt)) {
                        libusbip::output("connect cancelled");
                        return err;
                }
                return ERROR_SUCCESS;
        }

        if (auto err = WSAGetLastError(); err != WSAEWOULDBLOCK) {
                libusbip::output("connect error {}", err);
                return err;
        }

        return wait_for_connect(s, evt, cancel_evt);
}

/*
 * GetAddrInfoExOverlappedResult is not called here: it only reads OVERLAPPED::Internal
 * without any cleanup or side effects, and its return value is ignored on cancellation.
 *
 * Sets state.abandoned to signal resolve() to intentionally leak the state
 * and prevent use-after-free by late-completing driver/service.
 */
void cancel_resolve(_Inout_ resolve_state &state)
{
        libusbip::output("GetAddrInfoEx cancelled");
        auto evt = state.ovlp.hEvent;

        if (is_signaled(evt)) {
                return;
        }

        if (auto &h = state.cancel) {
                if (auto err = GetAddrInfoExCancel(&h)) {
                        libusbip::output("GetAddrInfoExCancel error {}", err);
                }
        }

        state.abandoned = true; // cannot verify completion, leak to prevent use-after-free

        switch (constexpr DWORD timeout = 5000;
                auto ret = WaitForSingleObject(evt, timeout)) {
        case WAIT_OBJECT_0:
                state.abandoned = false;
                break;
        case WAIT_TIMEOUT:
                libusbip::output("GetAddrInfoExCancel timed out after {}ms, leaking resolve state", timeout);
                break;
        default:
                assert(ret == WAIT_FAILED);
                auto err = GetLastError();
                libusbip::output("WaitForSingleObject failed on cancellation -> {}, error {}, leaking resolve state", ret, err);
        }
}

/*
 * INFINITE is safe here: the Windows DNS Client service enforces its own query retry
 * and timeout policies (~15-30s), signaling the overlapped event with an error code
 * like WSAHOST_NOT_FOUND if unreachable. Cancellation via cancel_evt is also supported.
 */
DWORD wait_for_resolve(_Inout_ resolve_state &state, _In_opt_ HANDLE cancel_evt)
{
        HANDLE events[] { state.ovlp.hEvent, cancel_evt };
        DWORD cnt = cancel_evt ? 2 : 1;

        auto on_cancelled = [&state]
        {
                cancel_resolve(state);
                return ERROR_CANCELLED;
        };

        for (;;) {
                switch (auto ret = WaitForMultipleObjectsEx(cnt, events, false, INFINITE, true)) {
                case WAIT_OBJECT_0:
                        if (is_signaled(cancel_evt)) {
                                return on_cancelled();
                        }
                        if (auto err = GetAddrInfoExOverlappedResult(&state.ovlp)) {
                                libusbip::output("GetAddrInfoExOverlappedResult error {}", err);
                                return err;
                        }
                        return ERROR_SUCCESS;
                case WAIT_OBJECT_0 + 1:
                        return on_cancelled();
                case WAIT_IO_COMPLETION: // see QueueUserAPC
                        continue;
                default:
                        assert(ret == WAIT_FAILED);

                        auto err = GetLastError();
                        libusbip::output("WaitForMultipleObjectsEx -> {}, error {}", ret, err);

                        cancel_resolve(state);
                        return err;
                }
        }
}

using addrinfo_ptr = std::unique_ptr<ADDRINFOEX, decltype(FreeAddrInfoEx)&>;

/*
 * Numeric IP addresses like "XXX.XXX.XXX.XXX" are resolved instantly.
 */
auto resolve(_In_ std::string_view hostname, _In_ std::string_view service, _In_opt_ HANDLE cancel_evt)
        -> std::expected<addrinfo_ptr, DWORD>
{
        auto host = utf8_to_wchar(hostname);
        auto svc = utf8_to_wchar(service);

        if (!(host && svc)) {
                auto err = !host ? host.error() : svc.error();
                libusbip::output("utf8_to_wchar('{}','{}') error {}", hostname, service, err);
                return std::unexpected(err);
        }

        auto state = std::make_unique<resolve_state>(std::move(*host), std::move(*svc));
        if (!*state) {
                auto err = GetLastError();
                libusbip::output("resolve_state initialization error {}", err);
                return std::unexpected(err);
        }

        libusbip::output("resolving {}:{}", hostname, service);

        DWORD err = GetAddrInfoEx(state->host.c_str(), state->svc.c_str(), NS_ALL, nullptr,
                                  &state->hints, &state->result, nullptr, &state->ovlp,
                                  nullptr, &state->cancel);

        switch (err) {
        case NO_ERROR:
                break;
        case WSA_IO_PENDING:
                err = wait_for_resolve(*state, cancel_evt);
                break;
        default:
                libusbip::output("GetAddrInfoEx error {}", err);
        }

        if (state->abandoned) {
                state.release(); // intentionally leak to prevent use-after-free by late-completing driver/service
                assert(err);
                return std::unexpected(err);
        }

        addrinfo_ptr ptr(state->result, FreeAddrInfoEx);
        if (err) {
                return std::unexpected(err);
        }
        return ptr;
}

DWORD finish_connect(_In_ SOCKET s)
{
        if (WSAEventSelect(s, WSA_INVALID_EVENT, 0)) { // cancel the association and selection of network events
                auto err = WSAGetLastError();
                libusbip::output("WSAEventSelect(0) error {}", err);
                return err;
        }

        return set_nonblock(s, false);
}

auto connect_to_addr(_In_ const ADDRINFOEX &r, _In_ WSAEVENT evt, _In_opt_ HANDLE cancel_event)
        -> std::expected<Socket, DWORD>
{
        if (!(r.ai_addr && r.ai_addrlen && r.ai_addrlen <= sizeof(sockaddr_storage))) {
                return std::unexpected(WSAEFAULT);
        }

        Socket sock(socket(r.ai_family, r.ai_socktype, r.ai_protocol));
        if (!sock) {
                auto err = WSAGetLastError();
                libusbip::output("socket(family={}) error {}", r.ai_family, err);
                return std::unexpected(err);
        }

        if (auto err = set_options(sock.get())) {
                return std::unexpected(err);
        }

        if (auto err = prepare_event(sock.get(), evt)) {
                return std::unexpected(err);
        }

        if (auto err = try_connect(sock.get(), evt, cancel_event, *r.ai_addr, static_cast<int>(r.ai_addrlen))) {
                return std::unexpected(err);
        }

        if (auto err = finish_connect(sock.get())) {
                return std::unexpected(err);
        }

        return sock;
}

auto connect_any(_In_ const ADDRINFOEX *ai, _In_ WSAEVENT evt, _In_opt_ HANDLE cancel_event)
        -> std::expected<Socket, DWORD>
{
        DWORD last_err = WSAHOST_NOT_FOUND;

        for (auto r = ai; r; r = r->ai_next) {
                auto sock = connect_to_addr(*r, evt, cancel_event);
                if (sock) {
                        return sock;
                }

                last_err = sock.error();
                if (last_err == ERROR_CANCELLED) {
                        break;
                }
        }

        return std::unexpected(last_err);
}

std::expected<Socket, DWORD> do_connect(
        _In_ std::string_view hostname, _In_ std::string_view service, _In_opt_ HANDLE cancel_event)
{
        if (hostname.empty() || service.empty()) {
                return std::unexpected(ERROR_INVALID_PARAMETER);
        }

        if (auto err = check_cancelled(cancel_event)) {
                return std::unexpected(err);
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

        return connect_any(ai->get(), evt.get(), cancel_event);
}

std::expected<usb_device, DWORD> recv_usb_device(_In_ SOCKET s)
{
        op_devlist_reply_extra extra;
        if (!recv(s, &extra, sizeof(extra))) {
                return std::unexpected(GetLastError());
        }
        byteswap(extra);

        auto &udev = extra.udev;

        auto speed = win_speed(static_cast<usb_device_speed>(udev.speed));
        if (!speed) {
                libusbip::output("invalid device speed: {}", udev.speed);
                return std::unexpected(ERROR_INVALID_DATA);
        }

        auto dev = as_usb_device(udev, *speed);
        dev.interfaces.reserve(udev.bNumInterfaces);

        for (int i = 0; i < udev.bNumInterfaces; ++i) {

                if (usbip_usb_interface intf; recv(s, &intf, sizeof(intf))) {
                        byteswap(intf);
                        dev.interfaces.push_back(as_usb_interface(intf));
                } else {
                        return std::unexpected(GetLastError());
                }
        }

        return dev;
}

std::expected<int, DWORD> get_device_count(_In_ SOCKET s)
{
        if (!send_op_common(s, OP_REQ_DEVLIST)) {
                return std::unexpected(GetLastError());
        }

        if (auto err = recv_op_common(s, OP_REP_DEVLIST)) {
                return std::unexpected(err);
        }

        op_devlist_reply reply;
        if (!recv(s, &reply, sizeof(reply))) {
                return std::unexpected(GetLastError());
        }
        byteswap(reply);

        if (reply.ndev > MAX_HUB_PORTS) {
                libusbip::output("the number of exportable devices {} is too large", reply.ndev);
                return std::unexpected(ERROR_INVALID_DATA);
        }

        return static_cast<int>(reply.ndev);
}

std::expected<std::vector<usb_device>, DWORD> get_exportable_devices(_In_ SOCKET s)
{
        auto cnt = get_device_count(s);
        if (!cnt) {
                return std::unexpected(cnt.error());
        }

        std::vector<usb_device> devices;
        devices.reserve(*cnt);

        for (int i = 0; i < *cnt; ++i) {
                if (auto dev = recv_usb_device(s)) {
                        devices.emplace_back(std::move(*dev));
                } else {
                        return std::unexpected(dev.error());
                }
        }

        return devices;
}

} // namespace


const char* usbip::get_tcp_port() noexcept
{
        return tcp_port;
}

NullableHandle usbip::create_cancel_event(_In_ bool initial_state) noexcept
{
        return NullableHandle(CreateEvent(nullptr, true, initial_state, nullptr));
}

Socket usbip::connect(
        _In_ std::string_view hostname, _In_ std::string_view service, _In_opt_ HANDLE cancel_event)
{
        if (auto res = do_connect(hostname, service, cancel_event)) {
                return std::move(*res);
        } else {
                SetLastError(res.error());
                return {};
        }
}

Socket usbip::connect(
        _In_ std::string_view hostname, _In_ std::string_view service, _In_ std::stop_token stoken)
{
        if (auto evt = stop_event(stoken)) {
                return connect(hostname, service, evt.get());
        }

        return {};
}

bool usbip::cancel_io(_In_ SOCKET s) noexcept
{
        if (s == INVALID_SOCKET) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
        }

        if (!shutdown(s, SD_BOTH)) {
                return true;
        }

        switch (auto err = WSAGetLastError()) {
        case WSAESHUTDOWN:
        case WSAENOTCONN:
                SetLastError(ERROR_SUCCESS);
                return true;
        default:
                SetLastError(err);
                return false;
        }
}

std::optional<std::vector<usb_device>> usbip::get_exportable_devices(_In_ SOCKET s)
{
        if (s == INVALID_SOCKET) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return std::nullopt;
        }

        auto res = ::get_exportable_devices(s);
        if (!res) {
                SetLastError(res.error());
                return std::nullopt;
        }

        return std::move(*res);
}
