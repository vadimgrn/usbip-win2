#pragma once

#include <libdrv\pageable.h>
#include <ntdef.h>

struct _USB_HUB_INFORMATION_EX;
struct _USB_PORT_CONNECTOR_PROPERTIES;

struct vhub_dev_t;
struct vpdo_dev_t;
struct ioctl_usbip_vhci_imported_dev;

PAGEABLE vpdo_dev_t *vhub_find_vpdo(vhub_dev_t &vhub, int port);

PAGEABLE bool vhub_attach_vpdo(vpdo_dev_t *vpdo);
PAGEABLE void vhub_detach_vpdo(vpdo_dev_t *vpdo);

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t &vhub, _USB_HUB_INFORMATION_EX &pinfo);
PAGEABLE NTSTATUS vhub_get_port_connector_properties(vhub_dev_t &vhub, _USB_PORT_CONNECTOR_PROPERTIES &r, ULONG &outlen);

NTSTATUS vhub_unplug_vpdo(vpdo_dev_t *vpdo);
PAGEABLE void vhub_unplug_all_vpdo(vhub_dev_t &vhub);

PAGEABLE NTSTATUS get_imported_devs(vhub_dev_t &vhub, ioctl_usbip_vhci_imported_dev *idevs, size_t cnt);
