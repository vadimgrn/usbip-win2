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

auto init(_Out_ vhci::plugin_hardware &r, _In_ const vhci::attach_args &args)
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

        r.out = decltype(r.out){}; // clear
        return true;
}

void assign(_Out_ std::vector<imported_device> &dst, _In_ const std::vector<vhci::imported_device> &src)
{
        dst.resize(src.size());

        for (size_t i = 0; i < src.size(); ++i) {
                auto &d = dst[i];
                auto &s = src[i];

                d.hub_port = s.out.port;

                d.hostname = s.host;
                assert(d.hostname.size() < ARRAYSIZE(s.host));

                d.service = s.service;
                assert(d.service.size() < ARRAYSIZE(s.service));

                d.busid = s.busid;
                assert(d.busid.size() < ARRAYSIZE(s.busid));

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

        std::vector<vhci::imported_device> v(TOTAL_PORTS);
        auto idevs_bytes = DWORD(v.size()*sizeof(v[0]));

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::get_imported_devices, nullptr, 0, 
                            v.data(), idevs_bytes, &BytesReturned, nullptr)) {
                assert(!(BytesReturned % sizeof(v[0])));
                v.resize(BytesReturned / sizeof(v[0]));
                assign(result, v);
                success = true; // result could be empty
        } else {
                success = false;
        }

        return result;
}

/*
 * @return hub port number, [1..TOTAL_PORTS]. Call GetLastError() if zero is returned. 
 */
int usbip::vhci::attach(_In_ HANDLE dev, _In_ const attach_args &args)
{
        plugin_hardware r;
        if (!init(r, args)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return 0;
        }

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::plugin_hardware, &r, sizeof(r), &r.out, sizeof(r.out), &BytesReturned, nullptr)) {
                assert(BytesReturned == sizeof(r.out));
                SetLastError(r.out.error);
                return r.out.port;
        }

        return 0;
}

/*
* @return call GetLastError() if false is returned
*/
bool usbip::vhci::detach(HANDLE dev, int port)
{
        plugout_hardware r { .port = port };

        DWORD BytesReturned; // must be set if the last arg is NULL
        auto ok = DeviceIoControl(dev, ioctl::plugout_hardware, &r, sizeof(r), nullptr, 0, &BytesReturned, nullptr);
        assert(!BytesReturned);

        return ok;
}
