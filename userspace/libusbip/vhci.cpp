/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "vhci.h"
#include "setupdi.h"
#include "output.h"
#include "last_error.h"

#include <resources\messages.h>

#include <initguid.h>
#include <usbip\vhci.h>

namespace
{

using namespace usbip;

auto walker_devpath(std::wstring &path, const GUID &guid, HDEVINFO dev_info, SP_DEVINFO_DATA *data)
{
        if (auto inf = get_intf_detail(dev_info, data, guid)) {
                assert(inf->cbSize == sizeof(*inf)); // this is not a size/length of DevicePath
                path = inf->DevicePath;
                return true;
        }

        return false;
}

auto init(_Out_ vhci::ioctl::plugin_hardware &r, _In_ const vhci::attach_args &args)
{
        struct {
                char *dst;
                size_t len;
                const std::string &src;
        } const v[] = {
                { r.busid, ARRAYSIZE(r.busid), args.busid },
                { r.service, ARRAYSIZE(r.service), args.service },
                { r.host, ARRAYSIZE(r.host), args.hostname },
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

void assign(_Out_ std::vector<imported_device> &dst, _In_ const vhci::imported_device *src, _In_ size_t cnt)
{
        assert(dst.empty());
        dst.reserve(cnt);

        for (size_t i = 0; i < cnt; ++i) {
                dst.resize(i + 1);
                auto &d = dst[i];
                auto &s = src[i];
                
                // imported_device_location 

                d.hub_port = s.port;

                d.busid = s.busid;
                assert(d.busid.size() < ARRAYSIZE(s.busid));

                d.service = s.service;
                assert(d.service.size() < ARRAYSIZE(s.service));

                d.hostname = s.host;
                assert(d.hostname.size() < ARRAYSIZE(s.host));

                // imported_device_properties

                d.devid = s.devid;
                d.speed = s.speed;

                d.vendor = s.vendor;
                d.product = s.product;
        }
}

} // namespace


/*
 * @return "" if the driver is not loaded 
 */
std::wstring usbip::vhci::get_path()
{
        std::wstring path;
        auto &guid = vhci::GUID_DEVINTERFACE_USB_HOST_CONTROLLER;

        auto f = [&path, &guid] (auto&&...args) 
        {
                return walker_devpath(path, guid, std::forward<decltype(args)>(args)...); 
        };

        traverse_intfdevs(guid, f);
        return path;
}

/*
 * Call GetLastError if returned handle is invalid.
 */
auto usbip::vhci::open(_In_ const std::wstring &path) -> Handle
{
        Handle h;

        if (path.empty()) { // get_path() failed
                SetLastError(ERROR_USBIP_VHCI_NOT_FOUND);
        } else {
                h.reset(CreateFile(path.c_str(),
                        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        }

        return h;
}

/*
 * Call GetLastError if result is false.
 */
std::vector<usbip::imported_device> usbip::vhci::get_imported_devices(_In_ HANDLE dev, _Out_ bool &success)
{
        std::vector<usbip::imported_device> result;

        ioctl::get_imported_devices *r{};
        std::vector<char> v(sizeof(*r) + (TOTAL_PORTS - ARRAYSIZE(r->devices))*sizeof(*r->devices));
        r = reinterpret_cast<ioctl::get_imported_devices*>(v.data());
        r->size = sizeof(*r);

        DWORD BytesReturned; // must be set if the last arg is NULL
        success = DeviceIoControl(dev, ioctl::GET_IMPORTED_DEVICES, r, sizeof(r->size), 
                                  v.data(), DWORD(v.size()), &BytesReturned, nullptr);

        if (!success) {
                return result;
        }

        const auto devices_offset = offsetof(ioctl::get_imported_devices, devices);

        assert(BytesReturned >= devices_offset);
        BytesReturned -= devices_offset;

        assert(!(BytesReturned % sizeof(*r->devices)));

        if (auto cnt = BytesReturned/sizeof(*r->devices)) {
                assign(result, r->devices, cnt);
        }

        SetLastError(r->error);
        return result;
}

/*
 * @return hub port number, [1..TOTAL_PORTS]. Call GetLastError() if zero is returned. 
 */
int usbip::vhci::attach(_In_ HANDLE dev, _In_ const attach_args &args)
{
        ioctl::plugin_hardware r {{ .size = sizeof(r) }};
        if (!init(r, args)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return 0;
        }

        const auto outlen = offsetof(ioctl::plugin_hardware, port) + sizeof(r.port);

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::PLUGIN_HARDWARE, &r, sizeof(r), &r, outlen, &BytesReturned, nullptr)) {
                assert(BytesReturned == outlen);
                SetLastError(r.error);
                return r.port;
        }

        return 0;
}

/*
* @return call GetLastError() if false is returned
*/
bool usbip::vhci::detach(HANDLE dev, int port)
{
        ioctl::plugout_hardware r { .port = port };
        r.size = sizeof(r);

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::PLUGOUT_HARDWARE, &r, sizeof(r), 
                            &r, sizeof(r.error), &BytesReturned, nullptr)) {
                assert(BytesReturned == sizeof(r.error));
                SetLastError(r.error);
                return !r.error;
        }

        return false;
}
