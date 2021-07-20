#pragma once

#include "basetype.h"
#include "vhci_dev.h"
#include "usbip_vhci_api.h"

PAGEABLE pvpdo_dev_t vhub_find_vpdo(pvhub_dev_t vhub, unsigned port);
PAGEABLE CHAR vhub_get_empty_port(pvhub_dev_t vhub);

PAGEABLE void vhub_attach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);
PAGEABLE void vhub_detach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);
PAGEABLE void vhub_get_hub_descriptor(pvhub_dev_t vhub, PUSB_HUB_DESCRIPTOR pdesc);

PAGEABLE NTSTATUS vhub_get_information_ex(pvhub_dev_t vhub, PUSB_HUB_INFORMATION_EX pinfo);
PAGEABLE NTSTATUS vhub_get_capabilities_ex(pvhub_dev_t vhub, PUSB_HUB_CAPABILITIES_EX pinfo);
PAGEABLE NTSTATUS vhub_get_port_connector_properties(pvhub_dev_t vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen);

PAGEABLE void vhub_mark_unplugged_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);
PAGEABLE void vhub_mark_unplugged_all_vpdos(pvhub_dev_t vhub);

PAGEABLE NTSTATUS vhub_get_ports_status(pvhub_dev_t vhub, ioctl_usbip_vhci_get_ports_status *st);
PAGEABLE NTSTATUS vhub_get_imported_devs(pvhub_dev_t vhub, pioctl_usbip_vhci_imported_dev_t idevs, PULONG poutlen);
