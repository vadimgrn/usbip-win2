#pragma once

#include "pageable.h"
#include "dev.h"
#include "usbip_vhci_api.h"

PAGEABLE vpdo_dev_t *vhub_find_vpdo(vhub_dev_t *vhub, int port);

PAGEABLE void vhub_attach_vpdo(vhub_dev_t *vhub, vpdo_dev_t *vpdo);
PAGEABLE void vhub_detach_vpdo_and_release_port(vhub_dev_t *vhub, vpdo_dev_t *vpdo);
PAGEABLE void vhub_get_hub_descriptor(vhub_dev_t *vhub, USB_HUB_DESCRIPTOR *pdesc);

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t *vhub, PUSB_HUB_INFORMATION_EX pinfo);
PAGEABLE NTSTATUS vhub_get_capabilities_ex(vhub_dev_t *vhub, PUSB_HUB_CAPABILITIES_EX pinfo);
PAGEABLE NTSTATUS vhub_get_port_connector_properties(vhub_dev_t *vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen);

PAGEABLE void vhub_mark_unplugged_vpdo(vhub_dev_t *vhub, vpdo_dev_t *vpdo);
PAGEABLE void vhub_mark_unplugged_all_vpdos(vhub_dev_t *vhub);

PAGEABLE NTSTATUS vhub_get_ports_status(vhub_dev_t *vhub, ioctl_usbip_vhci_get_ports_status *st);
PAGEABLE NTSTATUS vhub_get_imported_devs(vhub_dev_t *vhub, ioctl_usbip_vhci_imported_dev *idevs, PULONG poutlen);

PAGEABLE int acquire_port(vhub_dev_t &vhub);
PAGEABLE NTSTATUS release_port(vhub_dev_t &vhub, int port, bool lock = true);

PAGEABLE int get_vpdo_count(const vhub_dev_t &vhub);

PAGEABLE inline bool is_port_acquired(const vhub_dev_t &vhub, int port)
{
	NT_ASSERT(port > 0);
	NT_ASSERT(port <= vhub.NUM_PORTS);
	return vhub.bm_ports & (1 << (port - 1));
}