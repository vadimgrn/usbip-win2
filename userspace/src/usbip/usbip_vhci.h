#pragma once

#include <vector>

#include "usbip_vhci_api.h"
#include "win_handle.h"

usbip::Handle usbip_vhci_driver_open();

std::vector<ioctl_usbip_vhci_imported_dev> usbip_vhci_get_imported_devs(HANDLE hdev);

int usbip_vhci_attach_device(HANDLE hdev, vhci_pluginfo_t *pi);
int usbip_vhci_detach_device(HANDLE hdev, int port);
