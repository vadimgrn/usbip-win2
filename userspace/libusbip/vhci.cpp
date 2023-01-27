#include "dbgcode.h"
#include "setupdi.h"
#include "strconv.h"

#include <cassert>
#include <string>
#include <system_error>

#include <spdlog\spdlog.h>

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

auto init(vhci::ioctl_plugin_hardware &r, const vhci::attach_args &args)
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
                if (auto err = strcpy_s(i.dst, i.len, i.src.c_str())) {
                        auto msg = std::generic_category().message(err);
                        spdlog::error("strcpy_s('{}') error #{} {}", i.src, err, msg);
                        return false;
                }
        }

        return true;
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

int usbip::vhci::attach(_In_ HANDLE dev, _In_ const attach_args &args)
{
        ioctl_plugin_hardware r{};
        if (!init(r, args)) {
                return make_error(ERR_INVARG);
        }

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::plugin_hardware, &r, sizeof(r), 
                            &r, sizeof(r.port), &BytesReturned, nullptr)) {
                assert(BytesReturned == sizeof(r.port));
                return r.port;
        }

        auto err = GetLastError();
        spdlog::error("DeviceIoControl error {:#x} {}", err, format_message(err));

        return make_error(ERR_GENERAL);
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
