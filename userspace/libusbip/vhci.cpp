#include "dbgcode.h"
#include "setupdi.h"
#include "strconv.h"

#include <cassert>
#include <string>

#include <spdlog\spdlog.h>

#include <initguid.h>
#include "vhci.h"

namespace
{

bool walker_devpath(std::wstring &path, const GUID &guid, HDEVINFO dev_info, SP_DEVINFO_DATA *data)
{
        if (auto inf = usbip::get_intf_detail(dev_info, data, guid)) {
                assert(inf->cbSize == sizeof(*inf)); // this is not a size/length of DevicePath
                path = inf->DevicePath;
                return true;
        }

        return false;
}

} // namespace


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

auto usbip::vhci::open(const std::wstring &path) -> Handle
{
        Handle h;

        if (path.empty()) { // get_path() failed
                spdlog::error("{}: vhci device not found, the driver is not loaded?", __func__);
                return h;
        }

        h.reset(CreateFile(path.c_str(), 
                GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

        if (!h) {
                auto err = GetLastError();
                spdlog::error("CreateFile error {:#x} {}", err, format_message(err));
        }

        return h;
}

auto usbip::vhci::get_imported_devs(HANDLE dev, bool &result) -> std::vector<ioctl_get_imported_devices>
{
        std::vector<ioctl_get_imported_devices> v(TOTAL_PORTS);
        auto idevs_bytes = DWORD(v.size()*sizeof(v[0]));

        DWORD BytesReturned; // must be set if the last arg is NULL

        if (DeviceIoControl(dev, ioctl::get_imported_devices, nullptr, 0, 
                            v.data(), idevs_bytes, &BytesReturned, nullptr)) {
                assert(!(BytesReturned % sizeof(v[0])));
                v.resize(BytesReturned / sizeof(v[0]));
                result = true;
        } else {
                auto err = GetLastError();
                spdlog::error("DeviceIoControl error {:#x} {}", err, format_message(err));
                v.clear();
                result = false;
        }

        return v;
}

bool usbip::vhci::attach(HANDLE dev, ioctl_plugin_hardware &r)
{
        DWORD BytesReturned; // must be set if the last arg is NULL

        auto ok = DeviceIoControl(dev, ioctl::plugin_hardware, &r, sizeof(r), &r, sizeof(r.port), &BytesReturned, nullptr);
        if (!ok) {
                auto err = GetLastError();
                spdlog::error("DeviceIoControl error {:#x} {}", err, format_message(err));
        }
 
        assert(BytesReturned == sizeof(r.port));
        return ok;
}

auto usbip::vhci::detach(HANDLE dev, int port) -> err_t
{
        ioctl_plugout_hardware r{ .port = port };

        DWORD BytesReturned; // must be set if the last arg is NULL
        if (DeviceIoControl(dev, ioctl::plugout_hardware, &r, sizeof(r), nullptr, 0, &BytesReturned, nullptr)) {
                return ERR_NONE;
        }

        auto err = GetLastError();
        spdlog::error("DeviceIoControl error {:#x} {}", err, format_message(err));

        switch (err) {
        case ERROR_FILE_NOT_FOUND:
                return ERR_NOTEXIST;
        case ERROR_INVALID_PARAMETER:
                return ERR_INVARG;
        default:
                return ERR_GENERAL;
        }
}
