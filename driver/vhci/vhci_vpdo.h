#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vpdo_select_config(vpdo_dev_t *vpdo, struct _URB_SELECT_CONFIGURATION *cfg);
PAGEABLE NTSTATUS vpdo_select_interface(vpdo_dev_t *vpdo, struct _URB_SELECT_INTERFACE *r);

PAGEABLE NTSTATUS vpdo_get_nodeconn_info(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION conninfo, PULONG poutlen);
PAGEABLE NTSTATUS vpdo_get_nodeconn_info_ex(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX conninfo, PULONG poutlen);
PAGEABLE NTSTATUS vpdo_get_nodeconn_info_ex_v2(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX_V2 conninfo, PULONG poutlen);