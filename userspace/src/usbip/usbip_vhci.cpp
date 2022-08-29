#include <cassert>
#include <string>

#include <initguid.h>

#include "usbip_common.h"
#include "usbip_setupdi.h"
#include "dbgcode.h"
#include "usbip_vhci.h"

namespace
{

struct Context
{
        GUID guid;
        std::string path;
};

int walker_devpath(HDEVINFO dev_info, SP_DEVINFO_DATA *data, devno_t, void *context)
{
        auto &ctx = *reinterpret_cast<Context*>(context);

        if (auto inf = get_intf_detail(dev_info, data, ctx.guid)) {
                ctx.path = inf->DevicePath;
                return true;
        }

        return false;
}

auto get_vhci_devpath(vdev_usb_t version)
{
        Context r{ usbip_guid(version) };
        traverse_intfdevs(walker_devpath, r.guid, &r);
        return r.path;
}

} // namespace


auto usbip::vhci_driver_open(vdev_usb_t version) -> Handle
{
        Handle h;

        auto devpath = get_vhci_devpath(version);
        if (devpath.empty()) {
                return h;
        }
        
        dbg("device path: %s", devpath.c_str());
        
        auto fh = CreateFile(devpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        h.reset(fh);

        return h;
}

std::vector<ioctl_usbip_vhci_imported_dev> usbip::vhci_get_imported_devs(HANDLE hdev)
{
        std::vector<ioctl_usbip_vhci_imported_dev> v(VHUB_NUM_PORTS + 1);
        auto idevs_bytes = DWORD(v.size()*sizeof(v[0]));

        if (!DeviceIoControl(hdev, IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES, nullptr, 0, v.data(), idevs_bytes, nullptr, nullptr)) {
                dbg("failed to get imported devices: 0x%lx", GetLastError());
                v.clear();
        }

        return v;
}

bool usbip::vhci_attach_device(HANDLE hdev, ioctl_usbip_vhci_plugin &r)
{
        auto ok = DeviceIoControl(hdev, IOCTL_USBIP_VHCI_PLUGIN_HARDWARE, &r, sizeof(r), &r, sizeof(r.port), nullptr, nullptr);
        if (!ok) {
                dbg("%s: DeviceIoControl error %#x", __func__, GetLastError());
        }
        return ok;
}

int usbip::vhci_detach_device(HANDLE hdev, int port)
{
        ioctl_usbip_vhci_unplug r{ port };

        if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE, &r, sizeof(r), nullptr, 0, nullptr, nullptr)) {
                return 0;
        }

        auto err = GetLastError();
        dbg("%s: DeviceIoControl error %#x", __func__, err);

        switch (err) {
        case ERROR_FILE_NOT_FOUND:
                return ERR_NOTEXIST;
        case ERROR_INVALID_PARAMETER:
                return ERR_INVARG;
        default:
                return ERR_GENERAL;
        }
}
