/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../vhci.h"
#include "../volume_isolation.h"
#include "../persistent.h"

#include <usbip/offsetof_ex.h>
#include "device_speed.h"
#include "output.h"

#include <resources/messages.h>
#include <cfgmgr32.h>

#include <initguid.h>
#include <usbip/vhci.h>

#include <span>
#include <random>
#include <atomic>
#include <memory>

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
        } else if (auto err = strncpy_s(dst, serial.data(), serial.size())) {
                libusbip::output("strncpy_s('{}') error #{} {}", serial, err, std::generic_category().message(err));
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
                if (auto err = strncpy_s(i.dst, i.len, i.src.data(), i.src.size())) {
                        libusbip::output("strncpy_s('{}') error #{} {}", i.src, err, 
                                          std::generic_category().message(err));

                        return ERROR_INVALID_PARAMETER;
                }
        }

        return ERROR_SUCCESS;
}

DWORD assign(_Inout_ vhci::imported_device_config &dst, _In_ const device_config &src)
{
        dst.wsk_events = src.recv_mode == receive_mode::low_latency;
        dst.iso_mode = src.iso_mode;

        if (auto err = assign(dst.serial, src.serial)) {
                return err;
        }

        return assign(dst.location, src.location);
}

DWORD assign(_Inout_ vhci::ioctl::plugin_hardware &r, _In_ const vhci::attach_args &args)
{
        return assign(r.config, args.config);
}

auto make_device_location(_In_ const vhci::imported_device_location &src)
{
        auto str = [] (const char *str, size_t maxlen)
        {
                return std::string(str, strnlen(str, maxlen));
        };

        return device_location {
                .hostname = str(src.host, std::size(src.host)),
                .service = str(src.service, std::size(src.service)),
                .busid = str(src.busid, std::size(src.busid)),
        };
}

auto make_device_config(_In_ const vhci::imported_device_config &c)
{
        return device_config {
                .location = make_device_location(c.location),
                .serial{ c.serial, strnlen(c.serial, std::size(c.serial)) },
                .recv_mode = c.wsk_events ? receive_mode::low_latency : receive_mode::zero_copy,
                .iso_mode = c.iso_mode,
        };
}

auto make_imported_device(_In_ const vhci::imported_device &d)
{
        return imported_device {
                .config = make_device_config(d.config),
                .port = d.port,
                .devid = d.hw.devid,
                .speed = win_speed(d.hw.speed).value_or(UsbLowSpeed),
                .vendor = d.hw.vendor,
                .product = d.hw.product,
        };
}

auto make_imported_devices(_In_ const std::span<const vhci::imported_device> devices)
{
        std::vector<imported_device> v;
        v.reserve(devices.size());

        for (auto &d: devices) {
                v.push_back(make_imported_device(d));
        }

        return v;
}

auto make_device_state(_In_ const vhci::device_state &r)
{
        return device_state {
                .device = make_imported_device(r.device),
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

std::optional<std::vector<usbip::imported_device>> usbip::vhci::get_imported_devices(_In_ HANDLE dev)
{
        std::optional<std::vector<usbip::imported_device>> devices;
        constexpr auto devices_offset = offsetof_ex(ioctl::get_imported_devices, devices);

        ioctl::get_imported_devices *r{};
        std::vector<char> buf;

        constexpr auto max_devices = 1024;
        bool ok = false;

        for (auto cnt = 4; cnt <= max_devices; cnt <<= 1) {
                buf.resize(ioctl::get_imported_devices_size(cnt));

                r = reinterpret_cast<ioctl::get_imported_devices*>(buf.data());
                r->size = sizeof(*r);

                if (DWORD BytesReturned{}; // must be set if the last arg is NULL
                    DeviceIoControl(dev, ioctl::GET_IMPORTED_DEVICES, r, sizeof(r->size), 
                                    buf.data(), DWORD(buf.size()), &BytesReturned, nullptr)) {

                        if (BytesReturned < devices_offset) [[unlikely]] {
                                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
                                return devices;
                        }
                                
                        buf.resize(BytesReturned);
                        ok = true;
                        break;

                } else if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                        return devices;
                }
        }

        if (!ok) {
                return devices;
        }

        if (auto devices_size = buf.size() - devices_offset;
            devices_size % sizeof(*r->devices)) { // reminder must be zero
                libusbip::output("{}: N*sizeof(imported_device) != {}", __func__, devices_size);
                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
        } else {
                auto cnt = devices_size/sizeof(*r->devices);
                devices = make_imported_devices({r->devices, cnt});
        }

        return devices;
}

int usbip::vhci::attach(_In_ HANDLE dev, _In_ const attach_args &args)
{
        ioctl::plugin_hardware r{};
        r.size = sizeof(r);

        if (auto err = assign(r, args)) {
                SetLastError(err);
                return 0;
        }

        auto ctl = args.once ? ioctl::PLUGIN_HARDWARE_ONCE : ioctl::PLUGIN_HARDWARE;
        constexpr auto outlen = ioctl::plugin_hardware::output_size();

        if (DWORD BytesReturned{}; // must be set if the last arg is NULL
            DeviceIoControl(dev, ctl, &r, sizeof(r), &r, outlen, &BytesReturned, nullptr)) {

                if (BytesReturned != outlen) [[unlikely]] {
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

        if (DWORD BytesReturned{}; // must be set if the last arg is NULL
            !DeviceIoControl(dev, ioctl::STOP_ATTACH_ATTEMPTS, &r, sizeof(r), &r, outlen, &BytesReturned, nullptr)) {
                return -1;
        } else if (BytesReturned != outlen) [[unlikely]] {
                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
                return -1;
        } else {
                assert(r.count >= 0);
                return r.count;
        }
}

bool usbip::vhci::detach(_In_ HANDLE dev, _In_ int port)
{
        if (port != port_all_closeonly) {
                cleanup_port_volumes(port);
        }

        ioctl::plugout_hardware r { .port = port };
        r.size = sizeof(r);

        DWORD BytesReturned{}; // must be set if the last arg is NULL
        return DeviceIoControl(dev, ioctl::PLUGOUT_HARDWARE, &r, sizeof(r), nullptr, 0, &BytesReturned, nullptr);
}

bool usbip::vhci::detach(_In_ int port)
{
        auto dev = open();
        if (!dev) {
                return false;
        }
        return detach(dev.get(), port);
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

namespace
{

struct async_attach_context
{
        Handle owned_dev;
        HANDLE dev{};
        vhci::attach_args args;
        HANDLE cancel_event{};
        vhci::attach_callback callback;
        HANDLE wait_handle{};
};

struct async_detach_context
{
        Handle owned_dev;
        HANDLE dev{};
        int port{};
        vhci::detach_callback callback;
};

} // namespace

bool usbip::vhci::attach_async(
        _In_opt_ HANDLE dev,
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_opt_ HANDLE cancel_event)
{
        if (!callback) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
        }

        if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) {
                SetLastError(ERROR_CANCELLED);
                return false;
        }

        auto ctx = std::make_unique<async_attach_context>(async_attach_context {
                .dev = dev,
                .args = args,
                .cancel_event = cancel_event,
                .callback = std::move(callback)
        });

        if (!ctx->dev || ctx->dev == INVALID_HANDLE_VALUE) {
                ctx->owned_dev = open();
                if (!ctx->owned_dev) {
                        return false;
                }
                ctx->dev = ctx->owned_dev.get();
        }

        auto ok = TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, PVOID param) noexcept {
                auto ctx = std::unique_ptr<async_attach_context>(
                        static_cast<async_attach_context*>(param));

                if (ctx->cancel_event) {
                        RegisterWaitForSingleObject(&ctx->wait_handle, ctx->cancel_event,
                                [](PVOID param, BOOLEAN) noexcept {
                                        auto dev = static_cast<HANDLE>(param);
                                        CancelIoEx(dev, nullptr);
                                }, ctx->dev, INFINITE, WT_EXECUTEONLYONCE);
                }

                auto port = attach(ctx->dev, ctx->args);
                auto err = port > 0 ? ERROR_SUCCESS : GetLastError();

                if (err == ERROR_SUCCESS && port > 0 && ctx->args.config.iso_mode != isolation::none) {
                        isolate_port_volumes(port, 4000, ctx->cancel_event);
                }

                if (ctx->wait_handle) {
                        UnregisterWait(ctx->wait_handle);
                        ctx->wait_handle = nullptr;
                }

                if (ctx->cancel_event && WaitForSingleObject(ctx->cancel_event, 0) == WAIT_OBJECT_0) {
                        if (err == ERROR_SUCCESS && port > 0) {
                                detach(ctx->dev, port);
                        }
                        err = ERROR_CANCELLED;
                        port = 0;
                }

                try {
                        ctx->callback(port, err);
                } catch (...) {
                }
        }, ctx.get(), nullptr);

        if (ok) {
                ctx.release();
                return true;
        }

        return false;
}

bool usbip::vhci::attach_async(
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_opt_ HANDLE cancel_event)
{
        return attach_async(nullptr, args, std::move(callback), cancel_event);
}

bool usbip::vhci::attach_async(
        _In_ HANDLE dev,
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_ std::stop_token stoken)
{
        if (stoken.stop_requested()) {
                SetLastError(ERROR_CANCELLED);
                return false;
        }

        if (!stoken.stop_possible()) {
                return attach_async(dev, args, std::move(callback), HANDLE{});
        }

        NullableHandle cancel_evt(CreateEvent(nullptr, true, false, nullptr));
        if (!cancel_evt) {
                return false;
        }

        struct async_stop_guard
        {
                NullableHandle evt;
                std::optional<std::stop_callback<std::function<void()>>> stop_cb;
        };

        auto guard = std::make_shared<async_stop_guard>();
        guard->evt = std::move(cancel_evt);
        guard->stop_cb.emplace(stoken, [h = guard->evt.get()] { SetEvent(h); });

        auto wrapped_cb = [callback = std::move(callback), guard](int port, DWORD err) {
                callback(port, err);
        };

        return attach_async(dev, args, std::move(wrapped_cb), guard->evt.get());
}

bool usbip::vhci::attach_async(
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_ std::stop_token stoken)
{
        return attach_async(nullptr, args, std::move(callback), stoken);
}

bool usbip::vhci::detach_async(
        _In_ int port,
        _In_ detach_callback callback,
        _In_opt_ HANDLE dev)
{
        if (!callback) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
        }

        auto ctx = std::make_unique<async_detach_context>(async_detach_context {
                .dev = dev,
                .port = port,
                .callback = std::move(callback)
        });

        if (!ctx->dev || ctx->dev == INVALID_HANDLE_VALUE) {
                ctx->owned_dev = open();
                if (!ctx->owned_dev) {
                        return false;
                }
                ctx->dev = ctx->owned_dev.get();
        }

        auto ok = TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, PVOID param) noexcept {
                auto ctx = std::unique_ptr<async_detach_context>(
                        static_cast<async_detach_context*>(param));

                auto ok = detach(ctx->dev, ctx->port);
                auto err = ok ? ERROR_SUCCESS : GetLastError();

                try {
                        ctx->callback(ok, err);
                } catch (...) {
                }
        }, ctx.get(), nullptr);

        if (ok) {
                ctx.release();
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
        } else {
                state = make_device_state(*r);
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

namespace
{

class state_monitor_impl final : public vhci::state_monitor,
                                 public std::enable_shared_from_this<state_monitor_impl>
{
public:
        state_monitor_impl(HANDLE dev, vhci::state_callback callback)
                : m_dev(dev), m_callback(std::move(callback))
        {
        }

        void set_owned_handle(Handle owned_dev)
        {
                m_owned_dev = std::move(owned_dev);
                m_dev = m_owned_dev.get();
        }

        ~state_monitor_impl() override
        {
                cancel();
        }

        void cancel() noexcept override
        {
                if (!m_cancelled.exchange(true, std::memory_order_acq_rel)) {
                        CancelIoEx(m_dev, nullptr);
                }
        }

        bool is_cancelled() const noexcept
        {
                return m_cancelled.load(std::memory_order_acquire);
        }

        void run()
        {
                while (!is_cancelled()) {
                        auto state = vhci::read_device_state(m_dev);
                        if (!state) {
                                auto err = GetLastError();
                                if (is_cancelled() || err == ERROR_OPERATION_ABORTED) {
                                        break;
                                }
                                try {
                                        m_callback({}, err);
                                } catch (...) {
                                }
                                break;
                        }

                        if (is_cancelled()) {
                                break;
                        }

                        try {
                                m_callback(*state, ERROR_SUCCESS);
                        } catch (...) {
                        }
                }
        }

private:
        Handle m_owned_dev;
        HANDLE m_dev{};
        vhci::state_callback m_callback;
        std::atomic<bool> m_cancelled{false};
};

} // namespace

vhci::state_monitor_ptr usbip::vhci::watch_device_states(
        _In_ HANDLE dev,
        _In_ state_callback callback)
{
        if (!dev || dev == INVALID_HANDLE_VALUE || !callback) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return nullptr;
        }

        auto monitor = std::make_shared<state_monitor_impl>(dev, std::move(callback));

        auto ok = TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, PVOID param) noexcept {
                auto holder = static_cast<state_monitor_impl*>(param)->shared_from_this();
                holder->run();
        }, monitor.get(), nullptr);

        if (!ok) {
                return nullptr;
        }

        return monitor;
}

vhci::state_monitor_ptr usbip::vhci::watch_device_states(_In_ state_callback callback)
{
        if (!callback) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return nullptr;
        }

        auto dev = open();
        if (!dev) {
                return nullptr;
        }

        auto raw_dev = dev.get();
        auto monitor = std::make_shared<state_monitor_impl>(raw_dev, std::move(callback));
        monitor->set_owned_handle(std::move(dev));

        auto ok = TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, PVOID param) noexcept {
                auto holder = static_cast<state_monitor_impl*>(param)->shared_from_this();
                holder->run();
        }, monitor.get(), nullptr);

        if (!ok) {
                return nullptr;
        }

        return monitor;
}

std::optional<std::vector<usbip::imported_device>> usbip::vhci::get_imported_devices()
{
        auto dev = open();
        if (!dev) {
                return std::nullopt;
        }
        return get_imported_devices(dev.get());
}

int usbip::vhci::attach(_In_ const attach_args &args)
{
        auto dev = open();
        if (!dev) {
                return 0;
        }
        return attach(dev.get(), args);
}

int usbip::vhci::stop_attach_attempts(_In_opt_ const device_location *location)
{
        auto dev = open();
        if (!dev) {
                return -1;
        }
        return stop_attach_attempts(dev.get(), location);
}

usbip::vhci::client::client()
        : m_dev(vhci::open())
{
}

usbip::vhci::client::client(_In_ bool overlapped)
        : m_dev(vhci::open(overlapped))
{
}

usbip::vhci::client::client(_In_ Handle dev) noexcept
        : m_dev(std::move(dev))
{
}

usbip::vhci::client::operator bool() const noexcept
{
        return bool(m_dev);
}

bool usbip::vhci::client::operator!() const noexcept
{
        return !m_dev;
}

HANDLE usbip::vhci::client::handle() const noexcept
{
        return m_dev.get();
}

bool usbip::vhci::client::open(_In_ bool overlapped)
{
        m_dev = vhci::open(overlapped);
        return bool(m_dev);
}

void usbip::vhci::client::close() noexcept
{
        m_dev.close();
}

std::optional<std::vector<usbip::imported_device>> usbip::vhci::client::get_imported_devices() const
{
        return m_dev ? vhci::get_imported_devices(m_dev.get()) : vhci::get_imported_devices();
}

std::optional<std::vector<usbip::device_config>> usbip::vhci::client::get_persistent() const
{
        return m_dev ? vhci::get_persistent(m_dev.get()) : vhci::get_persistent();
}

bool usbip::vhci::client::set_persistent(_In_ const std::vector<device_config> &devices)
{
        return m_dev ? vhci::set_persistent(m_dev.get(), devices) : vhci::set_persistent(devices);
}

int usbip::vhci::client::attach(_In_ const attach_args &args)
{
        return m_dev ? vhci::attach(m_dev.get(), args) : vhci::attach(args);
}

bool usbip::vhci::client::attach_async(
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_opt_ HANDLE cancel_event)
{
        return vhci::attach_async(args, std::move(callback), cancel_event);
}

bool usbip::vhci::client::attach_async(
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_ std::stop_token stoken)
{
        return vhci::attach_async(args, std::move(callback), stoken);
}

int usbip::vhci::client::stop_attach_attempts(_In_opt_ const device_location *location)
{
        return m_dev ? vhci::stop_attach_attempts(m_dev.get(), location) : vhci::stop_attach_attempts(location);
}

bool usbip::vhci::client::detach(_In_ int port)
{
        return m_dev ? vhci::detach(m_dev.get(), port) : vhci::detach(port);
}

bool usbip::vhci::client::detach_async(_In_ int port, _In_ detach_callback callback)
{
        return vhci::detach_async(port, std::move(callback), nullptr);
}

vhci::state_monitor_ptr usbip::vhci::client::watch_device_states(_In_ state_callback callback)
{
        return vhci::watch_device_states(std::move(callback));
}
