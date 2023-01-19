#include <libusbip\common.h>
#include <libusbip\dbgcode.h>
#include <libusbip\setupdi.h>

#include <cassert>
#include <string>

#include <initguid.h>
#include "vhci.h"

namespace
{

using namespace usbip;

bool walker_devpath(std::wstring &path, const GUID &guid, HDEVINFO dev_info, SP_DEVINFO_DATA *data)
{
        if (auto inf = get_intf_detail(dev_info, data, guid)) {
                assert(inf->cbSize == sizeof(*inf)); // this is not a size/length of DevicePath
                path = inf->DevicePath;
                return true;
        }

        return false;
}

auto get_vhci_devpath()
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

} // namespace


auto usbip::vhci_driver_open() -> Handle
{
        Handle h;

        auto devpath = get_vhci_devpath();
        if (devpath.empty()) {
                return h;
        }
        
        dbg("device path: %S", devpath.c_str());
        
        auto fh = CreateFile(devpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        h.reset(fh);
        return h;
}

auto usbip::vhci_get_imported_devs(HANDLE hdev, bool &result) -> std::vector<vhci::ioctl_imported_dev>
{
        std::vector<vhci::ioctl_imported_dev> v(vhci::TOTAL_PORTS);
        auto idevs_bytes = DWORD(v.size()*sizeof(v[0]));

        DWORD BytesReturned; // must be set if the last arg is NULL

        if (DeviceIoControl(hdev, vhci::ioctl::get_imported_devices, nullptr, 0, v.data(), idevs_bytes, &BytesReturned, nullptr)) {
                assert(!(BytesReturned % sizeof(v[0])));
                v.resize(BytesReturned / sizeof(v[0]));
                result = true;
        } else {
                dbg("DeviceIoControl error %#lx", GetLastError());
                v.clear();
                result = false;
        }

        return v;
}

bool usbip::vhci_attach_device(HANDLE hdev, vhci::ioctl_plugin &r)
{
        DWORD BytesReturned; // must be set if the last arg is NULL

        auto ok = DeviceIoControl(hdev, vhci::ioctl::plugin_hardware, &r, sizeof(r), &r, sizeof(r.port), &BytesReturned, nullptr);
        if (!ok) {
                dbg("DeviceIoControl error %#lx", GetLastError());
        }
 
        assert(BytesReturned == sizeof(r.port));
        return ok;
}

int usbip::vhci_detach_device(HANDLE hdev, int port)
{
        vhci::ioctl_plugout r{ port };

        DWORD BytesReturned; // must be set if the last arg is NULL
        if (DeviceIoControl(hdev, vhci::ioctl::plugout_hardware, &r, sizeof(r), nullptr, 0, &BytesReturned, nullptr)) {
                return 0;
        }

        auto err = GetLastError();
        dbg("DeviceIoControl error %#lx", err);

        switch (err) {
        case ERROR_FILE_NOT_FOUND:
                return ERR_NOTEXIST;
        case ERROR_INVALID_PARAMETER:
                return ERR_INVARG;
        default:
                return ERR_GENERAL;
        }
}
