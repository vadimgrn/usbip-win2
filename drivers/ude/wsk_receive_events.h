/*
 * Copyright (c) 2026, Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/codeseg.h>
#include <libdrv/wdm_cpp.h>
#include <libdrv/wdf_cpp.h>

#include <UdeCxTypes.h>

struct _WSK_DATA_INDICATION;

namespace usbip::event
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS receive(_In_opt_ void *SocketContext, _In_ ULONG Flags, _In_opt_ _WSK_DATA_INDICATION *DataIndication,
        _In_ SIZE_T BytesIndicated, _Inout_ SIZE_T *BytesAccepted);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS disconnect(_In_opt_ void *SocketContext, _In_ ULONG flags);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS start_receive_data(_In_ UDECXUSBDEVICE device);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference stop_receive_data(_In_ UDECXUSBDEVICE device, _Inout_ bool &socket_closed);

} // namespace usbip::event
