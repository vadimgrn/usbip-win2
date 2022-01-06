#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS vpdo_select_config(vpdo_dev_t *vpdo, _URB_SELECT_CONFIGURATION *cfg);
PAGEABLE NTSTATUS vpdo_select_interface(vpdo_dev_t *vpdo, _URB_SELECT_INTERFACE *r);

PAGEABLE NTSTATUS vpdo_get_nodeconn_info(vpdo_dev_t *vpdo, USB_NODE_CONNECTION_INFORMATION_EX &ci, ULONG *poutlen, bool ex);
PAGEABLE NTSTATUS vpdo_get_nodeconn_info_ex_v2(vpdo_dev_t *vpdo, USB_NODE_CONNECTION_INFORMATION_EX_V2 &ci, ULONG *poutlen);
