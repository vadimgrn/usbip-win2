/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/codeseg.h>
#include <libdrv/wdm_cpp.h>
#include <libdrv/wdf_cpp.h>

#include <UdeCxTypes.h>

namespace usbip
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS start_receive_data_irp(_In_ UDECXUSBDEVICE device);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference stop_receive_data_irp(_In_ UDECXUSBDEVICE device, _Inout_ bool &socket_closed);

} // namespace usbip
