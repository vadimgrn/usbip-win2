/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

namespace usbip
{

struct vhci_ctx;
struct device_ctx;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS copy(
        _Out_ char *host, _In_ USHORT host_sz, _In_ const UNICODE_STRING &uhost,
        _Out_ char *service, _In_ USHORT service_sz, _In_ const UNICODE_STRING &uservice,
        _Out_ char *busid, _In_ USHORT busid_sz, _In_ const UNICODE_STRING &ubusid);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void load_imported_devices(_In_ vhci_ctx *vhci);

} // namespace usbip
