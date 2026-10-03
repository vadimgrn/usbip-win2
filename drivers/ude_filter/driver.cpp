/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 * 
 * @see https://github.com/desowin/usbpcap/tree/master/USBPcapDriver
 */

#include <ntifs.h>

#include "driver.h"
#include "trace.h"
#include "driver.tmh"

#include "irp.h"
#include "pnp.h"
#include "int_dev_ctrl.h"

#include <libdrv/remove_lock.h>
#include <libdrv/security.h>

using namespace usbip;
using namespace libdrv;

namespace
{

_Function_class_(DRIVER_UNLOAD)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void driver_unload(_In_ DRIVER_OBJECT *drvobj)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(drvobj));
	WPP_CLEANUP(drvobj);
}

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS irp_complete(
        _In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
        auto fltr = static_cast<filter_ext*>(context);
        remove_lock_guard{fltr->remove_lock, adopt_lock, irp};

        if (irp->PendingReturned) {
                IoMarkIrpPending(irp);
        }

        return ContinueCompletion;
}

_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_OTHER)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto dispatch_lower(_In_ DEVICE_OBJECT *devobj, _Inout_ IRP *irp)
{
	auto &fltr = *get_filter_ext(devobj);

	remove_lock_guard lck(fltr.remove_lock, irp);
	if (!lck) {
		auto err = lck.status();
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}

        IoCopyCurrentIrpStackLocationToNext(irp);
        IoSetCompletionRoutine(irp, irp_complete, &fltr, true, true, true);

        lck.clear();
        return IoCallDriver(fltr.target, irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto is_user_access_allowed(_In_ const filter_ext &fltr, _In_ IRP *irp)
{
        PAGED_CODE();

        auto &dev = fltr.device;
        sid_data sid;

        auto allowed = NT_SUCCESS(get_requestor_sid(irp, sid)) &&
                       (is_system_sid(sid) || equal_sid(sid, dev.owner_sid));

        if (!allowed) {
                Trace(TRACE_LEVEL_WARNING, "%04x: requestor SID != owner SID", ptr04x(fltr.self));
        }

        return allowed;
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto is_session_access_allowed(_In_ const filter_ext &fltr, _In_ IRP *irp)
{
        PAGED_CODE();

        auto &dev = fltr.device;
        auto id = get_requestor_session_id(irp);

        auto allowed = id == dev.session_id || 
                       id == system_session_id ||
                       id == invalid_session_id;

        if (!allowed) {
                Trace(TRACE_LEVEL_WARNING, "%04x: requestor session_id %lu != owner session_id %lu",
                        ptr04x(fltr.self), id, dev.session_id);
        }

        return allowed;
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto is_access_allowed(_In_ const filter_ext &fltr, _In_ IRP *irp)
{
        PAGED_CODE();
        NT_ASSERT(!fltr.is_hub);

        switch (auto &dev = fltr.device; dev.iso_mode) {
        case isolation::none:
                return true;
        case isolation::session:
                return is_session_access_allowed(fltr, irp);
        case isolation::user:
                return is_user_access_allowed(fltr, irp);
        default:
                Trace(TRACE_LEVEL_ERROR, "%04x, invalid iso_mode %d",
                        ptr04x(fltr.self), static_cast<int>(dev.iso_mode));

                return false;
        }
}

/*
 * An IO_REMOVE_LOCK is not required for IRP_MJ_CREATE:
 * - The I/O Manager holds a reference to the device object for the duration of the dispatch.
 * - The PnP Manager will not issue IRP_MN_REMOVE_DEVICE while open handles or create requests exist.
 */
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_CREATE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto create(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
        PAGED_CODE();

        if (auto &fltr = *get_filter_ext(devobj);
            fltr.is_hub || irp->RequestorMode == KernelMode || is_access_allowed(fltr, irp)) {
                return libdrv::ForwardIrp(fltr.target, irp);
        }

        return CompleteRequest(irp, STATUS_ACCESS_DENIED);
}

} // namespace


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT EXTERN_C NTSTATUS DriverEntry(_In_ DRIVER_OBJECT *drvobj, _In_ UNICODE_STRING *RegistryPath)
{
	ExInitializeDriverRuntime(0); // @see ExAllocatePool2

	WPP_INIT_TRACING(drvobj, RegistryPath);
	Trace(TRACE_LEVEL_INFORMATION, "%04x, '%!USTR!'", ptr04x(drvobj), RegistryPath);

	drvobj->DriverUnload = driver_unload;
	drvobj->DriverExtension->AddDevice = add_device;

	for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		drvobj->MajorFunction[i] = dispatch_lower;
	}

        drvobj->MajorFunction[IRP_MJ_CREATE] = create;
	drvobj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = int_dev_ctrl;
        drvobj->MajorFunction[IRP_MJ_PNP] = pnp;

	return STATUS_SUCCESS;
}
