/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../vhci.h"
#include "../persistent.h"

#include "volume_isolation.h"
#include "strconv.h"
#include "output.h"
#include "last_error.h"
#include "device_speed.h"

#include <usbip/offsetof_ex.h>
#include <resources/messages.h>

#include <cfgmgr32.h>
#include <strsafe.h>

#include <initguid.h>
#include <usbip/vhci.h>
#include "sid.h"

#include <span>
#include <random>

namespace
{

using namespace usbip;

static_assert(static_cast<int>(vhci::port_all) == vhci::ioctl::PORT_ALL);
static_assert(static_cast<int>(vhci::port_all_closeonly) == vhci::ioctl::PORT_ALL_CLOSEONLY);

constexpr auto map_attach_error(_In_ DWORD err)
{
        switch (err) {
        case ERROR_NOT_FOUND: // STATUS_NOT_FOUND, WskGetAddressInfo
        case ERROR_NO_MATCH:  // STATUS_NO_MATCH,  WskGetAddressInfo
                err = WSAHOST_NOT_FOUND;
                break;
        case ERROR_SEM_TIMEOUT: // STATUS_IO_TIMEOUT, WskConnect
                err = WSAETIMEDOUT;
                break;
/*
        case ERROR_CONNECTION_REFUSED: // STATUS_CONNECTION_REFUSED, WskConnect
                err = WSAECONNREFUSED;
                break;
*/
        }

        return err;
}

auto assign(_Inout_ char (&dst)[SERIAL_BUFSZ], _In_ const std::string &serial)
{
        if (!validate_device_serial(serial)) {
                *dst = '\0';
        } else if (auto hr = StringCchCopyA(dst, std::size(dst), serial.c_str()); FAILED(hr)) {
                libusbip::output("StringCchCopyA('{}') error {:#x}", serial, static_cast<unsigned long>(hr));
        } else {
                return USBIP_ERROR_SUCCESS;
        }

        return USBIP_ERROR_SERIAL_NUMBER;
}

auto assign(_Inout_ vhci::imported_device_location &dst, _In_ const device_location &src)
{
        struct {
                char *dst;
                size_t len;
                const std::string &src;
        } const v[] = {
                { dst.busid, std::size(dst.busid), src.busid },
                { dst.service, std::size(dst.service), src.service },
                { dst.host, std::size(dst.host), src.hostname },
        };

        for (auto &i: v) {
                if (auto hr = StringCchCopyA(i.dst, i.len, i.src.c_str()); FAILED(hr)) {
                        libusbip::output("StringCchCopyA('{}', max={}) error {:#x}",
                                         i.src, i.len, static_cast<unsigned long>(hr));

                        return ERROR_INVALID_PARAMETER;
                }
        }

        return ERROR_SUCCESS;
}

DWORD assign(_Inout_ vhci::isolation_data &dst, _In_ const device_config &src)
{
        dst = { .mode = src.iso_mode };

        if (src.user.empty()) {
                return ERROR_SUCCESS;
        } else if (src.iso_mode != vhci::isolation::user) {
                libusbip::output("user '{}' specified without user isolation", src.user);
                return ERROR_INVALID_PARAMETER;
        } else if (auto sid = parse_sid(src.user)) {
                dst.owner.sid = *sid;
                return ERROR_SUCCESS;
        } else {
                libusbip::output("parse_sid('{}') error #{}", src.user, sid.error());
                return sid.error();
        }
}

DWORD assign(_Inout_ vhci::imported_device_config &dst, _In_ const device_config &src)
{
        dst.wsk_events = src.recv_mode == receive_mode::low_latency;

        if (auto err = assign(dst.iso, src)) {
                return err;
        }

        if (auto err = assign(dst.serial, src.serial)) {
                return err;
        }

        return assign(dst.location, src.location);
}

std::optional<device_location> make_device_location(_In_ const vhci::imported_device_location &src)
{
        auto host = to_string_view(src.host);
        auto service = to_string_view(src.service);
        auto busid = to_string_view(src.busid);

        if (host.empty() || service.empty() || busid.empty()) {
                libusbip::output("empty or unterminated imported_device_location");
                return std::nullopt;
        }

        return device_location {
                .hostname{host},
                .service{service},
                .busid{busid}
        };
}

std::optional<device_config> make_device_config(_In_ const vhci::imported_device_config &c)
{
        auto loc = make_device_location(c.location);
        if (!loc) {
                return std::nullopt;
        }

        auto serial = to_string_view(c.serial);

        if (serial.empty() && *c.serial) {
                libusbip::output("unterminated imported_device_config::serial");
                return std::nullopt;
        }

        return device_config {
                .location = std::move(*loc),
                .serial{serial},
                .recv_mode = c.wsk_events ? receive_mode::low_latency : receive_mode::zero_copy,
                .iso_mode = c.iso.mode,
                .user = c.iso.owner.has_sid() ? to_string(c.iso.owner.sid) : std::string{},
        };
}

std::optional<imported_device> make_imported_device(_In_ const vhci::imported_device &d)
{
        auto cfg = make_device_config(d.config);
        if (!cfg) {
                return std::nullopt;
        }

        return imported_device {
                .config = std::move(*cfg),
                .port = d.port,
                .devid = d.hw.devid,
                .speed = win_speed(d.hw.speed).value_or(UsbLowSpeed),
                .vendor = d.hw.vendor,
                .product = d.hw.product,
        };
}

auto make_imported_devices(_In_ const std::span<const vhci::imported_device> devices)
        -> std::optional<std::vector<imported_device>>
{
        std::vector<imported_device> v;
        v.reserve(devices.size());

        for (auto &d: devices) {
                if (auto dev = make_imported_device(d)) {
                        v.push_back(std::move(*dev));
                } else {
                        return std::nullopt;
                }
        }

        return v;
}

std::optional<device_state> make_device_state(_In_ const vhci::device_state &r)
{
        auto dev = make_imported_device(r.device);
        if (!dev) {
                return std::nullopt;
        }

        return device_state {
                .device = std::move(*dev),
                .state = static_cast<state>(r.state),
                .source_id = r.source_id
        };
}

auto get_path()
{
        auto guid = const_cast<GUID*>(&vhci::GUID_DEVINTERFACE_USBIP_VHCI);
        std::wstring path;

        for (std::wstring multi_sz; true; ) {

                ULONG cch;
                if (auto err = CM_Get_Device_Interface_List_Size(&cch, guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)) {
                        libusbip::output("CM_Get_Device_Interface_List_Size error #{}", err);
                        auto code = CM_MapCrToWin32Err(err, ERROR_INVALID_PARAMETER);
                        SetLastError(code);
                        return path;
                }

                multi_sz.resize(cch); // "path1\0path2\0pathn\0\0"

                switch (auto err = CM_Get_Device_Interface_List(guid, nullptr, multi_sz.data(), cch, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)) {
                case CR_SUCCESS:
                        if (auto v = split_multi_sz(multi_sz); auto n = v.size()) {
                                if (n == 1) {
                                        path = v.front();
                                        assert(!path.empty());
                                } else {
                                        libusbip::output("CM_Get_Device_Interface_List: {} paths returned", n);
                                        SetLastError(USBIP_ERROR_DEVICE_INTERFACE_LIST);
                                }
                        } else {
                                assert(multi_sz.size() == 1); // if not found, returns CR_SUCCESS and ""
                                assert(!multi_sz.front());
                                SetLastError(USBIP_ERROR_VHCI_NOT_FOUND);
                        }
                        return path;
                case CR_BUFFER_SMALL:
                        break;
                default:
                        libusbip::output("CM_Get_Device_Interface_List error #{}", err);
                        auto code = CM_MapCrToWin32Err(err, ERROR_NOT_ENOUGH_MEMORY);
                        SetLastError(code);
                        return path;
                }
        }
}

auto read_imported_devices(_In_ HANDLE dev, _Inout_ std::vector<char> &buf)
        -> std::expected<std::span<vhci::imported_device>, DWORD>
{
        DWORD bytes_returned{}; // must be set if the last arg is NULL

        for (ULONG cnt = 4; ; cnt = std::min(cnt << 1, static_cast<ULONG>(MAX_HUB_PORTS))) {

                buf.resize(vhci::ioctl::get_imported_devices_size(cnt));

                auto r = reinterpret_cast<vhci::ioctl::get_imported_devices*>(buf.data());
                r->size = sizeof(*r);

                if (DeviceIoControl(dev, vhci::ioctl::GET_IMPORTED_DEVICES, r, sizeof(r->size),
                                    buf.data(), DWORD(buf.size()), &bytes_returned, nullptr)) {
                        break;
                }

                if (auto err = GetLastError(); !(err == ERROR_INSUFFICIENT_BUFFER && cnt < MAX_HUB_PORTS)) {
                        return std::unexpected(err);
                }
        }

        constexpr auto devices_offset = offsetof_ex(vhci::ioctl::get_imported_devices, devices);

        if (bytes_returned < devices_offset) [[unlikely]] {
                return std::unexpected(USBIP_ERROR_DRIVER_RESPONSE);
        }

        auto &r = *reinterpret_cast<vhci::ioctl::get_imported_devices*>(buf.data());
        auto devices_size = bytes_returned - devices_offset;

        if (devices_size % sizeof(*r.devices)) [[unlikely]] { // remainder must be zero
                libusbip::output("{}: N*sizeof(imported_device) != {}", __func__, devices_size);
                return std::unexpected(USBIP_ERROR_DRIVER_RESPONSE);
        }

        return std::span(r.devices, devices_size/sizeof(*r.devices));
}

void cleanup_isolated_volumes(_In_ HANDLE dev, _In_ int port)
{
        auto devices = vhci::get_imported_devices(dev);
        if (!devices) {
                return;
        }

        for (auto &d: *devices) {
                if ((port == vhci::port_all || d.port == port) && d.config.iso_mode != isolation::none) {
                        cleanup_port_volumes(d.port);
                }
        }
}

} // namespace


int usbip::get_device_serial_maxlen() noexcept
{
        return SERIAL_BUFSZ - 1; // without '\0'
}

bool usbip::validate_device_serial(_In_ const std::string &serial) noexcept
{
        auto sz = ssize(serial);
        auto valid = sz <= get_device_serial_maxlen();

        if (valid) {
                auto len = is_ascii_alnum(serial.data(), sz);
                valid = len >= 0 && len == sz;
        }

        if (!valid) {
                SetLastError(USBIP_ERROR_SERIAL_NUMBER);
        }

        return valid;
}

std::string usbip::generate_device_serial()
{
        constexpr std::string_view chars =
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "0123456789";

        thread_local std::mt19937 rng(std::random_device{}());
        thread_local std::uniform_int_distribution<std::size_t> dist(0, chars.size() - 1);

        auto len = get_device_serial_maxlen();
        std::string serial(len, '\0');

        std::ranges::generate(serial, [&chars] { return chars[dist(rng)]; } );

        assert(validate_device_serial(serial));
        return serial;
}

const char* usbip::vhci::get_state_str(_In_ usbip::state state) noexcept
{
        static_assert(int(state::unplugged) == int(vhci::state::unplugged));
        static_assert(int(state::connecting) == int(vhci::state::connecting));
        static_assert(int(state::connected) == int(vhci::state::connected));
        static_assert(int(state::plugged) == int(vhci::state::plugged));
        static_assert(int(state::disconnected) == int(vhci::state::disconnected));
        static_assert(int(state::unplugging) == int(vhci::state::unplugging));

        static_assert(int(state::unplugged) == 0);
        static_assert(int(state::connecting) == 1);
        static_assert(int(state::connected) == 2);
        static_assert(int(state::plugged) == 3);
        static_assert(int(state::disconnected) == 4);
        static_assert(int(state::unplugging) == 5);

        static constexpr const char* const v[] = {
                "unplugged", "connecting", "connected", "plugged", "disconnected", "unplugging"
        };

        auto idx = static_cast<int>(state);
        return idx >= 0 && idx < std::ssize(v) ? v[idx] : "";
}

auto usbip::vhci::open(_In_ bool overlapped) -> Handle
{
        DWORD FlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
        if (overlapped) {
                FlagsAndAttributes |= FILE_FLAG_OVERLAPPED;
        }

        Handle h;

        if (auto path = get_path(); !path.empty()) {
                h.reset(CreateFile(path.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, // lpSecurityAttributes
                                OPEN_EXISTING,
                                FlagsAndAttributes,
                                nullptr)); // hTemplateFile
        }

        return h;
}

auto usbip::vhci::get_imported_devices(_In_ HANDLE dev)
        -> std::optional<std::vector<usbip::imported_device>>
{
        std::vector<char> buf;
        auto devs = read_imported_devices(dev, buf);
        if (!devs) {
                SetLastError(devs.error());
                return std::nullopt;
        }

        auto devices = make_imported_devices(*devs);
        if (!devices) {
                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
        }
        return devices;
}

auto usbip::vhci::get_port_storage(_In_ HANDLE dev, _In_ int port)
        -> std::expected<port_storage, DWORD>
{
        std::vector<char> buf;
        auto devs = read_imported_devices(dev, buf);
        if (!devs) {
                return std::unexpected(devs.error());
        }

        for (const auto &d: *devs) {
                if (d.port == port) {
                        return d.hw.is_non_storage() ? port_storage::non_storage : port_storage::maybe_storage;
                }
        }

        return port_storage::detached;
}

/*
 * Note: attach does not call isolate_port_volumes(). The IOCTL only initiates
 * virtual hardware plug-in and returns immediately. Storage volumes are
 * enumerated asynchronously by Windows PnP and MountMgr, requiring polling,
 * timeouts, and cancellation tokens handled by the caller (e.g. CLI or GUI).
 */
int usbip::vhci::attach(_In_ HANDLE dev, _In_ const attach_args &args)
{
        ioctl::plugin_hardware r{};
        r.size = sizeof(r);

        if (auto err = assign(r.config, args.config)) {
                SetLastError(err);
                return 0;
        }

        auto ctl = args.once ? ioctl::PLUGIN_HARDWARE_ONCE : ioctl::PLUGIN_HARDWARE;
        constexpr auto outlen = ioctl::plugin_hardware::output_size();

        if (DWORD bytes_returned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ctl, &r, sizeof(r), &r, outlen, &bytes_returned, nullptr)) {

                if (bytes_returned != outlen) [[unlikely]] {
                        SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
                        return 0;
                }

                assert(r.port > 0);
                return r.port;
        }

        auto err = GetLastError();
        if (auto code = map_attach_error(err); code != err) {
                SetLastError(code);
        }

        return 0;
}

int usbip::vhci::stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *location)
{
        ioctl::stop_attach_attempts r{};
        r.size = sizeof(r);

        if (location) {
                if (auto err = assign(r.location, *location)) {
                        SetLastError(err);
                        return -1;
                }
        }

        constexpr auto outlen = ioctl::stop_attach_attempts::output_size();

        if (DWORD bytes_returned; // must be set if the last arg is NULL
            !DeviceIoControl(dev, ioctl::STOP_ATTACH_ATTEMPTS, &r, sizeof(r), &r, outlen, &bytes_returned, nullptr)) {
                return -1;
        } else if (bytes_returned != outlen) [[unlikely]] {
                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
                return -1;
        } else {
                assert(r.count >= 0);
                return r.count;
        }
}

/*
 * Clean up session-isolated drive letters before hardware plugout.
 * Unlike \GLOBAL??, session DosDevices links are not removed automatically
 * by MountMgr/PnP when the PDO is removed. Furthermore, DevNode
 * traversal requires the device tree to still be intact.
 * The query is instantaneous (no waiting) as the devices already exist.
 */
bool usbip::vhci::detach(_In_ HANDLE dev, _In_ int port)
{
        if (port > 0 || port == port_all) {
                cleanup_isolated_volumes(dev, port);
        }

        ioctl::plugout_hardware r { .port = port };
        r.size = sizeof(r);

        DWORD bytes_returned; // must be set if the last arg is NULL
        return DeviceIoControl(dev, ioctl::PLUGOUT_HARDWARE, &r, sizeof(r), nullptr, 0, &bytes_returned, nullptr);
}

bool usbip::vhci::cancel_io(_In_ HANDLE dev, _In_opt_ OVERLAPPED *overlapped) noexcept
{
        if (CancelIoEx(dev, overlapped)) {
                return true;
        }
        if (GetLastError() == ERROR_NOT_FOUND) {
                SetLastError(ERROR_SUCCESS);
                return true;
        }
        return false;
}

DWORD usbip::vhci::get_device_state_size() noexcept
{
        return sizeof(vhci::device_state);
}

std::optional<usbip::device_state> usbip::vhci::get_device_state(_In_ const void *data, _In_ DWORD length)
{
        std::optional<usbip::device_state> state;

        auto r = reinterpret_cast<const vhci::device_state*>(data);
        assert(get_device_state_size() == sizeof(*r));

        if (auto ok = r && length == sizeof(*r); !ok) {
                SetLastError(ERROR_INVALID_PARAMETER);
        } else if (r->size != sizeof(*r)) {
                SetLastError(USBIP_ERROR_ABI);
        } else if (state = make_device_state(*r); !state) {
                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
        }

        return state;
}

/*
 * ReadFile returns TRUE for STATUS_END_OF_FILE.
 * @see UDE driver, EVT_WDF_IO_QUEUE_IO_READ
 */
std::optional<usbip::device_state> usbip::vhci::read_device_state(_In_ HANDLE dev)
{
        std::optional<usbip::device_state> state;
        DWORD actual;

        if (vhci::device_state r; !ReadFile(dev, &r, sizeof(r), &actual, nullptr)) {
                //
        } else if (!actual) {
                SetLastError(ERROR_HANDLE_EOF);
        } else {
                state = get_device_state(&r, actual);
        }

        return state;
}
