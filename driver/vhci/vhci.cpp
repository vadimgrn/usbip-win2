#include "vhci.h"
#include <wdm.h>
#include "trace.h"
#include "vhci.tmh"

#include <libdrv\wsk_cpp.h>

#include "power.h"
#include "pnp.h"
#include "pnp_add.h"
#include "irp.h"
#include "wmi.h"
#include "vhub.h"
#include "ioctl.h"
#include "wsk_context.h"
#include "internal_ioctl.h"

namespace
{

_Enum_is_bitflag_ enum { INIT_WSK_CTX_LIST = 1 };
unsigned int g_init_flags;

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
PAGEABLE auto vhci_complete(_In_ PDEVICE_OBJECT devobj, _In_ PIRP Irp, const char *what)
{
	PAGED_CODE();

	auto vdev = to_vdev(devobj);

	if (vdev->PnPState == pnp_state::Removed) {
		Trace(TRACE_LEVEL_INFORMATION, "%s(%!vdev_type_t!): no such device", what, vdev->type);
		return CompleteRequest(Irp, STATUS_NO_SUCH_DEVICE);
	}

	TraceMsg("%!vdev_type_t!", vdev->type);

	Irp->IoStatus.Information = 0;
	return CompleteRequest(Irp);
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_CREATE)
PAGEABLE NTSTATUS vhci_create(_In_ PDEVICE_OBJECT devobj, _In_ PIRP Irp)
{
	return vhci_complete(devobj, Irp, __func__);
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_CLOSE)
PAGEABLE NTSTATUS vhci_close(_In_ PDEVICE_OBJECT devobj, _In_ PIRP Irp)
{
	return vhci_complete(devobj, Irp, __func__);
}

_Function_class_(DRIVER_UNLOAD)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGEABLE void DriverUnload(_In_ DRIVER_OBJECT *drvobj)
{
	PAGED_CODE();

	TraceMsg("%04x", ptr4log(drvobj));

        wsk::shutdown();

	if (g_init_flags & INIT_WSK_CTX_LIST) {
		ExDeleteLookasideListEx(&wsk_context_list);
	}

	if (auto buf = Globals.RegistryPath.Buffer) {
	        ExFreePoolWithTag(buf, USBIP_VHCI_POOL_TAG);
        }

        WPP_CLEANUP(drvobj);
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGEABLE auto save_registry_path(const UNICODE_STRING *RegistryPath)
{
        PAGED_CODE();
        
        auto &path = Globals.RegistryPath;
	USHORT max_len = RegistryPath->Length + sizeof(UNICODE_NULL);

	path.Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_NON_PAGED|POOL_FLAG_UNINITIALIZED, max_len, USBIP_VHCI_POOL_TAG);
	if (!path.Buffer) {
		Trace(TRACE_LEVEL_CRITICAL, "Can't allocate %hu bytes", max_len);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

        NT_ASSERT(!path.Length);
	path.MaximumLength = max_len;

	RtlCopyUnicodeString(&path, RegistryPath);
	TraceDbg("%!USTR!", &path);

        return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGEABLE auto init_lookaside_lists()
{
	PAGED_CODE();

	if (auto err = init_wsk_context_list()) {
		return err;
	} else {
		g_init_flags |= INIT_WSK_CTX_LIST;
	}

	return STATUS_SUCCESS;
}

} // namespace


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
__declspec(code_seg("INIT"))
extern "C" NTSTATUS DriverEntry(_In_ DRIVER_OBJECT *drvobj, _In_ UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	WPP_INIT_TRACING(drvobj, RegistryPath);
	TraceMsg("%04x", ptr4log(drvobj));

	if (auto err = init_lookaside_lists()) {
		Trace(TRACE_LEVEL_CRITICAL, "init_lookaside_lists %!STATUS!", err);
		DriverUnload(drvobj);
		return err;
	}

        if (auto err = wsk::initialize()) {
                Trace(TRACE_LEVEL_CRITICAL, "WskRegister %!STATUS!", err);
                DriverUnload(drvobj);
                return err;
        }
        
        if (auto err = save_registry_path(RegistryPath)) {
                DriverUnload(drvobj);
                return err;
	}

	drvobj->MajorFunction[IRP_MJ_CREATE] = vhci_create;
	drvobj->MajorFunction[IRP_MJ_CLOSE] = vhci_close;
	drvobj->MajorFunction[IRP_MJ_PNP] = vhci_pnp;
	drvobj->MajorFunction[IRP_MJ_POWER] = vhci_power;
	drvobj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = vhci_ioctl;
	drvobj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = vhci_internal_ioctl;
	drvobj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = vhci_system_control;

	drvobj->DriverUnload = DriverUnload;
	drvobj->DriverExtension->AddDevice = vhci_add_device;

	return STATUS_SUCCESS;
}
