/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

namespace usbip
{

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS ForwardIrpAndWait(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ForwardIrpAsync(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status = STATUS_SUCCESS);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void CompleteRequestAsCancelled(_In_ IRP *irp);

inline auto list_entry(_In_ IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}

inline auto get_irp(_In_ LIST_ENTRY *entry)
{
	return CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
}

} // namespace usbip
