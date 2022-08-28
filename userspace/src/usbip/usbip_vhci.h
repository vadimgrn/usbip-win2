#pragma once

#include <vector>

#include "usbip_vhci_api.h"
#include "win_handle.h"

usbip::Handle usbip_vhci_driver_open(usbip_hci version);

std::vector<ioctl_usbip_vhci_imported_dev> usbip_vhci_get_imported_devs(HANDLE hdev);

bool usbip_vhci_attach_device(HANDLE hdev, ioctl_usbip_vhci_plugin &r);
int usbip_vhci_detach_device(HANDLE hdev, int port);
