/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "irp.h"

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS libdrv::ForwardIrp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
        NT_ASSERT(devobj);
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(devobj, irp);
}

/*
 * A caller must complete the IRP after this call.
 *
 * IoForwardIrpSynchronously only returns FALSE if no next stack location is available in the IRP.
 * This means your driver is at the bottom of the device stack, or the IRP was poorly constructed
 * by the sender. Because the function failed, the IRP was never passed down, and you are
 * responsible for handling its failure lifecycle.
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS libdrv::ForwardIrpSynchronously(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(devobj);

	auto &status = irp->IoStatus.Status;

	if (!IoForwardIrpSynchronously(devobj, irp)) {
                status = STATUS_INVALID_DEVICE_REQUEST;
                irp->IoStatus.Information = 0;
        }

	return status;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS libdrv::CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status)
{
	irp->IoStatus.Status = status;
	CompleteRequest(irp);
	return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS libdrv::sync_irp::ctor(_In_ CCHAR StackSize, _In_ bool ChargeQuota)
{
        if (*this) {
                return STATUS_ALREADY_INITIALIZED;
        }

        m_irp = IoAllocateIrp(StackSize, ChargeQuota);
        if (!m_irp) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        KeInitializeEvent(&m_event, SynchronizationEvent, false);
        set_completion_routine();

        return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void libdrv::sync_irp::dtor()
{
        if (auto ptr = static_cast<IRP*>(InterlockedExchangePointer(reinterpret_cast<PVOID*>(&m_irp), nullptr))) {
                IoFreeIrp(ptr);
        }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void libdrv::sync_irp::reset()
{
        if (*this) {
                IoReuseIrp(m_irp, STATUS_SUCCESS);
                KeClearEvent(&m_event);
                set_completion_routine();
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS libdrv::sync_irp::completion(_In_ DEVICE_OBJECT*, _In_ IRP*, _In_ void *context)
{
        auto &self = *static_cast<sync_irp*>(context);
        KeSetEvent(&self.m_event, IO_NO_INCREMENT, false);
        return StopCompletion;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS libdrv::sync_irp::wait_for_completion(_Inout_ NTSTATUS &status)
{
        PAGED_CODE();

        if (!*this) {
                return status = STATUS_INVALID_DEVICE_STATE;
        }

        if (status == STATUS_PENDING) {
                NT_VERIFY(!KeWaitForSingleObject(&m_event, Executive, KernelMode, false, nullptr));
                status = m_irp->IoStatus.Status;
        }

        return status;
}

