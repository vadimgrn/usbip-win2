/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "..\vhci.h"

#include "device_speed.h"
#include "output.h"

#include <resources\messages.h>
#include <cfgmgr32.h>

#include <initguid.h>
#include <usbip\vhci.h>

namespace
{

using namespace usbip;

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

auto assign(_Out_ vhci::imported_device_location &dst, _In_ const device_location &src)
{
        struct {
                char *dst;
                size_t len;
                const std::string &src;
        } const v[] = {
                { dst.busid, ARRAYSIZE(dst.busid), src.busid },
                { dst.service, ARRAYSIZE(dst.service), src.service },
                { dst.host, ARRAYSIZE(dst.host), src.hostname },
        };

        for (auto &i: v) {
                if (auto err = strncpy_s(i.dst, i.len, i.src.data(), i.src.size())) {
                        libusbip::output("strncpy_s('{}') error #{} {}", i.src, err, 
                                          std::generic_category().message(err));
                        return false;
                }
        }

        return true;
}

void assign(_Out_ device_location &dst, _In_ const vhci::imported_device_location &src)
{
        struct {
                std::string &dst;
                const char *src;
                size_t maxlen;
        } const v[] = {
                { dst.hostname, src.host, ARRAYSIZE(src.host) },
                { dst.service, src.service, ARRAYSIZE(src.service) },
                { dst.busid, src.busid, ARRAYSIZE(src.busid) },
        };

        for (auto &i: v) {
                i.dst.assign(i.src, strnlen(i.src, i.maxlen));
        }
}

auto make_imported_device(_In_ const vhci::imported_device &s)
{
        imported_device d { 
                // imported_device_location
                .port = s.port,
                // imported_device_properties
                .devid = s.devid,
                .speed = win_speed(s.speed),
                .vendor = s.vendor,
                .product = s.product,
        };

        assign(d.location, s);
        return d;
}

auto make_device_state(_In_ const vhci::device_state &r)
{
        return device_state {
                .device = make_imported_device(r),
                .state = static_cast<state>(r.state)
        };
}

void assign(_Out_ std::vector<imported_device> &dst, _In_ const vhci::imported_device *src, _In_ size_t cnt)
{
        assert(dst.empty());
        dst.reserve(cnt);

        for (size_t i = 0; i < cnt; ++i) {
                dst.push_back(make_imported_device(src[i]));
        }
}

auto get_path()
{
        auto guid = const_cast<GUID*>(&vhci::GUID_DEVINTERFACE_USB_HOST_CONTROLLER);
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

        const char* v[] = { "unplugged", "connecting", "connected", "plugged", "disconnected", "unplugging" };

        auto idx = static_cast<int>(state);
        return idx >= 0 && idx < ARRAYSIZE(v) ? v[idx] : "";
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

std::vector<usbip::imported_device> usbip::vhci::get_imported_devices(_In_ HANDLE dev, _Out_ bool &success)
{
        success = false;
        std::vector<usbip::imported_device> result;

        constexpr auto devices_offset = offsetof(ioctl::get_imported_devices, devices);

        ioctl::get_imported_devices *r{};
        std::vector<char> buf;

        for (auto cnt = 4; true; cnt <<= 1) {
                buf.resize(ioctl::get_imported_devices_size(cnt));

                r = reinterpret_cast<ioctl::get_imported_devices*>(buf.data());
                r->size = sizeof(*r);

                if (DWORD BytesReturned{}; // must be set if the last arg is NULL
                    DeviceIoControl(dev, ioctl::GET_IMPORTED_DEVICES, r, sizeof(r->size), 
                                    buf.data(), DWORD(buf.size()), &BytesReturned, nullptr)) {

                        if (BytesReturned < devices_offset) [[unlikely]] {
                                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
                                return result;
                        }
                                
                        buf.resize(BytesReturned);
                        break;

                } else if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                        return result;
                }
        }

        auto devices_size = buf.size() - devices_offset;
        success = !(devices_size % sizeof(*r->devices));

        if (!success) {
                libusbip::output("{}: N*sizeof(imported_device) != {}", __func__, devices_size);
                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
        } else if (auto cnt = devices_size/sizeof(*r->devices)) {
                assign(result, r->devices, cnt);
        }

        return result;
}

int usbip::vhci::attach(_In_ HANDLE dev, _In_ const device_location &location)
{
        ioctl::plugin_hardware r {{ .size = sizeof(r) }};
        if (!assign(r, location)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return 0;
        }

        constexpr auto outlen = offsetof(ioctl::plugin_hardware, port) + sizeof(r.port);

        if (DWORD BytesReturned{}; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::PLUGIN_HARDWARE, &r, sizeof(r), &r, outlen, &BytesReturned, nullptr)) {

                if (BytesReturned != outlen) [[unlikely]] {
                        SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
                } else {
                        assert(r.port > 0);
                        return r.port;
                }
        }

        auto err = GetLastError();
        if (auto code = map_attach_error(err); code != err) {
                SetLastError(code);
        }

        return 0;
}

int usbip::vhci::stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *location)
{
        ioctl::stop_attach_attempts r {{ .size = sizeof(r) }};

        if (location && !assign(r, *location)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return -1;
        }
        
        if (DWORD BytesReturned{}; // must be set if the last arg is NULL
            !DeviceIoControl(dev,ioctl::STOP_ATTACH_ATTEMPTS, &r, sizeof(r), &r, sizeof(r), &BytesReturned, nullptr)) {
                return -1;
        } else if (BytesReturned != sizeof(r)) [[unlikely]] {
                SetLastError(USBIP_ERROR_DRIVER_RESPONSE);
                return -1;
        } else {
                assert(r.count >= 0);
                return r.count;
        }
}

bool usbip::vhci::detach(_In_ HANDLE dev, _In_ int port)
{
        ioctl::plugout_hardware r { .port = port };
        r.size = sizeof(r);

        DWORD BytesReturned{}; // must be set if the last arg is NULL
        return DeviceIoControl(dev, ioctl::PLUGOUT_HARDWARE, &r, sizeof(r), nullptr, 0, &BytesReturned, nullptr);
}

USBIP_API DWORD usbip::vhci::get_device_state_size() noexcept
{
        return sizeof(vhci::device_state);
}

USBIP_API bool usbip::vhci::get_device_state(
        _Inout_ usbip::device_state &result, _In_ const void *data, _In_ DWORD length)
{
        auto r = reinterpret_cast<const vhci::device_state*>(data);
        assert(get_device_state_size() == sizeof(*r));

        if (!(r && length == sizeof(*r))) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
        } else if (r->size != sizeof(*r)) {
                SetLastError(USBIP_ERROR_ABI);
                return false;
        }

        result = make_device_state(*r);
        return true;
}

/*
 * ReadFile returns TRUE for STATUS_END_OF_FILE.
 * @see UDE driver, EVT_WDF_IO_QUEUE_IO_READ
 */
bool usbip::vhci::read_device_state(_In_ HANDLE dev, _Inout_ usbip::device_state &result)
{
        vhci::device_state r;

        if (DWORD actual; !ReadFile(dev, &r, sizeof(r), &actual, nullptr)) {
                return false;
        } else if (!actual) {
                SetLastError(ERROR_HANDLE_EOF);
                return false;
        } else {
                return get_device_state(result, &r, actual);
        }
}
