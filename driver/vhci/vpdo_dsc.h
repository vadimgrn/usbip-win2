#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS get_descr_from_nodeconn(vpdo_dev_t *vpdo, USB_DESCRIPTOR_REQUEST &r, ULONG &outlen);
