#pragma once

#include <vector>

#include <usbip\vhci.h>
#include <libusbip\win_handle.h>

namespace usbip
{

Handle vhci_driver_open();

std::vector<vhci::ioctl_imported_dev> vhci_get_imported_devs(HANDLE hdev, bool &result);

bool vhci_attach_device(HANDLE hdev, vhci::ioctl_plugin &r);
int vhci_detach_device(HANDLE hdev, int port);

} // namespace usbip
