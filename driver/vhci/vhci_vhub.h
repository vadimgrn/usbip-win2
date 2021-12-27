#pragma once

#include "basetype.h"
#include "vhci_dev.h"
#include "usbip_vhci_api.h"

PAGEABLE vpdo_dev_t * vhub_find_vpdo(vhub_dev_t *vhub, ULONG port);
PAGEABLE CHAR vhub_get_empty_port(vhub_dev_t *vhub);

PAGEABLE void vhub_attach_vpdo(vhub_dev_t *vhub, vpdo_dev_t * vpdo);
PAGEABLE void vhub_detach_vpdo(vhub_dev_t *vhub, vpdo_dev_t * vpdo);
PAGEABLE void vhub_get_hub_descriptor(vhub_dev_t *vhub, PUSB_HUB_DESCRIPTOR pdesc);

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t *vhub, PUSB_HUB_INFORMATION_EX pinfo);
PAGEABLE NTSTATUS vhub_get_capabilities_ex(vhub_dev_t *vhub, PUSB_HUB_CAPABILITIES_EX pinfo);
PAGEABLE NTSTATUS vhub_get_port_connector_properties(vhub_dev_t *vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen);

PAGEABLE void vhub_mark_unplugged_vpdo(vhub_dev_t *vhub, vpdo_dev_t * vpdo);
PAGEABLE void vhub_mark_unplugged_all_vpdos(vhub_dev_t *vhub);

PAGEABLE NTSTATUS vhub_get_ports_status(vhub_dev_t *vhub, ioctl_usbip_vhci_get_ports_status *st);
PAGEABLE NTSTATUS vhub_get_imported_devs(vhub_dev_t *vhub, pioctl_usbip_vhci_imported_dev_t idevs, PULONG poutlen);
