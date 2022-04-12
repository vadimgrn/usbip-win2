#include <cassert>
#include <string>

#include <initguid.h>

#include "usbip_common.h"
#include "usbip_setupdi.h"
#include "dbgcode.h"
#include "usbip_vhci.h"

namespace
{

int walker_devpath(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
        auto id_hw = get_id_hw(dev_info, pdev_info_data);
        if (!id_hw || (_stricmp(id_hw, "usbipwin\\vhci") && _stricmp(id_hw, "root\\vhci_ude"))) {
                dbg("invalid hw id: %s", id_hw ? id_hw : "");
                free(id_hw);
                return 0;
        }
        free(id_hw);

        auto pdev_interface_detail = get_intf_detail(dev_info, pdev_info_data, &GUID_DEVINTERFACE_VHCI_USBIP);
        if (!pdev_interface_detail) {
                return 0;
        }

        static_cast<std::string*>(ctx)->assign(pdev_interface_detail->DevicePath);
        free(pdev_interface_detail);
        return 1;
}

auto get_vhci_devpath()
{
        std::string path;
        traverse_intfdevs(walker_devpath, &GUID_DEVINTERFACE_VHCI_USBIP, &path);
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

int
usbip_vhci_attach_device(HANDLE hdev, struct vhci_pluginfo_t* pluginfo)
{
        if (!DeviceIoControl(hdev, IOCTL_USBIP_VHCI_PLUGIN_HARDWARE,
                pluginfo, pluginfo->size, pluginfo, sizeof(*pluginfo), nullptr , nullptr)) {
                DWORD err = GetLastError();
                if (err == ERROR_HANDLE_EOF) {
                        return ERR_PORTFULL;
                }
                dbg("usbip_vhci_attach_device: DeviceIoControl failed: err: 0x%lx", GetLastError());
                return ERR_GENERAL;
        }

        return 0;
}

int
usbip_vhci_detach_device(HANDLE hdev, int port)
{
        ioctl_usbip_vhci_unplug unplug{ port };

        if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE, &unplug, sizeof(unplug), nullptr, 0, nullptr, nullptr)) {
                return 0;
        }

        auto err = GetLastError();
        dbg("unplug error: 0x%lx", err);

        switch (err) {
        case ERROR_FILE_NOT_FOUND:
                return ERR_NOTEXIST;
        case ERROR_INVALID_PARAMETER:
                return ERR_INVARG;
        default:
                return ERR_GENERAL;
        }
}
