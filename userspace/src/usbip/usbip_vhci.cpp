#include <cassert>
#include <string>

#include <initguid.h>

#include "usbip_common.h"
#include "usbip_setupdi.h"
#include "dbgcode.h"
#include "usbip_vhci.h"

namespace
{

int walker_devpath(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data, devno_t, void *ctx)
{
        auto id_hw = get_id_hw(dev_info, dev_info_data);

        if (_stricmp(id_hw.c_str(), "usbipwin\\vhci")) { // usbip_vhci.inf
                dbg("invalid hw id: %s", id_hw.c_str());
                return 0;
        }

        if (auto inf = get_intf_detail(dev_info, dev_info_data, GUID_DEVINTERFACE_VHCI_USBIP)) {
                *static_cast<std::string*>(ctx) = inf->DevicePath;
                return 1;
        }

        return 0;
}

auto get_vhci_devpath()
{
        std::string path;
        traverse_intfdevs(walker_devpath, GUID_DEVINTERFACE_VHCI_USBIP, &path);
        return path;
}

int usbip_vhci_get_ports_status(HANDLE hdev, ioctl_usbip_vhci_get_ports_status &st)
{
        DWORD len = 0;

        if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_GET_PORTS_STATUS, nullptr, 0, &st, sizeof(st), &len, nullptr)) {
                if (len == sizeof(st)) {
                        return 0;
                }
        }

        return ERR_GENERAL;
}

int get_n_max_ports(HANDLE hdev)
{
        ioctl_usbip_vhci_get_ports_status st;
        auto err = usbip_vhci_get_ports_status(hdev, st);
        return err < 0 ? err : st.n_max_ports;
}

} // namespace


usbip::Handle usbip_vhci_driver_open()
{
        usbip::Handle h;

        auto devpath = get_vhci_devpath();
        if (devpath.empty()) {
                return h;
        }
        
        dbg("device path: %s", devpath.c_str());
        
        auto fh = CreateFile(devpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        h.reset(fh);

        return h;
}

std::vector<ioctl_usbip_vhci_imported_dev> usbip_vhci_get_imported_devs(HANDLE hdev)
{
        std::vector<ioctl_usbip_vhci_imported_dev> idevs;

        auto n_max_ports = get_n_max_ports(hdev);
        if (n_max_ports < 0) {
                dbg("failed to get the number of used ports: %s", dbg_errcode(n_max_ports));
                return idevs;
        }

        idevs.resize(n_max_ports + 1);
        auto idevs_bytes = DWORD(idevs.size()*sizeof(idevs[0]));

        if (!DeviceIoControl(hdev, IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES, nullptr, 0, idevs.data(), idevs_bytes, nullptr, nullptr)) {
                dbg("failed to get imported devices: 0x%lx", GetLastError());
                idevs.clear();
        }

        return idevs;
}

bool usbip_vhci_attach_device(HANDLE hdev, ioctl_usbip_vhci_plugin &r)
{
        auto ok = DeviceIoControl(hdev, IOCTL_USBIP_VHCI_PLUGIN_HARDWARE, &r, sizeof(r), &r, sizeof(r.port), nullptr, nullptr);
        if (!ok) {
                dbg("%s: DeviceIoControl error %#x", __func__, GetLastError());
        }
        return ok;
}

int usbip_vhci_detach_device(HANDLE hdev, int port)
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
