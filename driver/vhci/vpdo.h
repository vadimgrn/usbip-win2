#pragma once

#include "pageable.h"
#include "dev.h"

NTSTATUS vpdo_select_config(vpdo_dev_t *vpdo, _URB_SELECT_CONFIGURATION *cfg);
NTSTATUS vpdo_select_interface(vpdo_dev_t *vpdo, _URB_SELECT_INTERFACE *r);

PAGEABLE NTSTATUS vpdo_get_nodeconn_info(vpdo_dev_t *vpdo, USB_NODE_CONNECTION_INFORMATION_EX &ci, ULONG &outlen, bool ex);
