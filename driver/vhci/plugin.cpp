#include "plugin.h"
#include "trace.h"
#include "plugin.tmh"

#include "strutil.h"
#include "vhub.h"
#include "pnp.h"
#include "usb_util.h"
#include "usbdsc.h"
#include "vhci.h"
#include "pnp_remove.h"

namespace
{

PAGEABLE void CsqInsertIrp(_In_ IO_CSQ *Csq, _In_ IRP *Irp)
{
	TraceCall("%p", Irp);
	auto vpdo = to_vpdo(Csq);
	InsertTailList(&vpdo->pending_irps, &Irp->Tail.Overlay.ListEntry);
}

PAGEABLE void CsqRemoveIrp(_In_ IO_CSQ* /*Csq*/, _In_ IRP *Irp)
{
	TraceCall("%p", Irp);
	auto le = &Irp->Tail.Overlay.ListEntry;
	RemoveEntryList(le);
	InitializeListHead(le);
}

PAGEABLE PIRP CsqPeekNextIrp(_In_ IO_CSQ *Csq, _In_ IRP *Irp, _In_ PVOID PeekContext)
{
	IRP *result{};

	auto &vpdo = *to_vpdo(Csq);
	auto head = &vpdo.pending_irps;

	for (auto next = Irp ? Irp->Tail.Overlay.ListEntry.Flink : head->Flink; next != head; next = next->Flink) 
	{
		auto irp = CONTAINING_RECORD(next, IRP, Tail.Overlay.ListEntry);
		auto irpstack = IoGetCurrentIrpStackLocation(irp);

		if (!PeekContext || static_cast<FILE_OBJECT*>(PeekContext) == irpstack->FileObject) {
			result = irp;
			break;
		}
	}

	TraceCall("%p", result);
	return result;
}

_IRQL_raises_(DISPATCH_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Acquires_lock_(CONTAINING_RECORD(Csq,vpdo_dev_t, csq)->QueueLock)
PAGEABLE void CsqAcquireLock(_In_ IO_CSQ *Csq, _Out_ PKIRQL Irql)
{
	auto &vpdo = *to_vpdo(Csq);
	KeAcquireSpinLock(&vpdo.pending_irps_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(Csq,vpdo_dev_t, csq)->QueueLock)
PAGEABLE void CsqReleaseLock(_In_ IO_CSQ *Csq, _In_ KIRQL Irql)
{
	auto &vpdo = *to_vpdo(Csq);
	KeReleaseSpinLock(&vpdo.pending_irps_lock, Irql);
}

PAGEABLE void CsqCompleteCanceledIrp(_In_ IO_CSQ*, _In_ IRP *Irp)
{
	TraceCall("%p", Irp);

	Irp->IoStatus.Status = STATUS_CANCELLED;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

PAGEABLE auto vhci_init_vpdo(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	vpdo.current_intf_num = 0;
	vpdo.current_intf_alt = 0;

	vpdo.PnPState = pnp_state::NotStarted;
	vpdo.PreviousPnPState = pnp_state::NotStarted;

	// vpdo usually starts its life at D3
	vpdo.DevicePowerState = PowerDeviceD3;
	vpdo.SystemPowerState = PowerSystemWorking;

	InitializeListHead(&vpdo.pending_irps);
	KeInitializeSpinLock(&vpdo.pending_irps_lock);

	if (auto err = IoCsqInitialize(&vpdo.csq,
				CsqInsertIrp,
				CsqRemoveIrp,
				CsqPeekNextIrp,
				CsqAcquireLock,
				CsqReleaseLock,
				CsqCompleteCanceledIrp)) 
	{
		Trace(TRACE_LEVEL_ERROR, "IoCsqInitialize %!STATUS!", err);
		return err;
	}

	InitializeListHead(&vpdo.head_urbr);
	InitializeListHead(&vpdo.head_urbr_pending);
	InitializeListHead(&vpdo.head_urbr_sent);
	KeInitializeSpinLock(&vpdo.lock_urbr);

	auto &Flags = vpdo.Self->Flags;
	Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	if (!vhub_attach_vpdo(&vpdo)) {
		Trace(TRACE_LEVEL_ERROR, "Can't acquire free usb port");
		return STATUS_END_OF_FILE;
	}

	Flags &= ~DO_DEVICE_INITIALIZING; // should be the last step in initialization
	return STATUS_SUCCESS;
}

PAGEABLE auto init_vpdo(vpdo_dev_t &vpdo, const USB_DEVICE_DESCRIPTOR &d)
{
	PAGED_CODE();

	if (is_valid_dsc(&d)) {
		NT_ASSERT(!is_valid_dsc(&vpdo.descriptor)); // first time initialization
		RtlCopyMemory(&vpdo.descriptor, &d, sizeof(d));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid device descriptor");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo.speed = get_usb_speed(d.bcdUSB);

	vpdo.bDeviceClass = d.bDeviceClass;
	vpdo.bDeviceSubClass = d.bDeviceSubClass;
	vpdo.bDeviceProtocol = d.bDeviceProtocol;

	return STATUS_SUCCESS;
}

PAGEABLE auto set_class_subclass_proto(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	auto d = dsc_find_next_intf(vpdo.actconfig, nullptr);
	if (!d) {
		Trace(TRACE_LEVEL_ERROR, "Interface descriptor not found");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo.bDeviceClass = d->bInterfaceClass;
	vpdo.bDeviceSubClass = d->bInterfaceSubClass;
	vpdo.bDeviceProtocol = d->bInterfaceProtocol;

	Trace(TRACE_LEVEL_INFORMATION, "Set Class(%#02x)/SubClass(%#02x)/Protocol(%#02x) from bInterfaceNumber %d, bAlternateSetting %d",
					vpdo.bDeviceClass, vpdo.bDeviceSubClass, vpdo.bDeviceProtocol,
					d->bInterfaceNumber, d->bAlternateSetting);

	return STATUS_SUCCESS;
}

/*
* Many devices have 0 usb class number in a device descriptor.
* 0 value means that class number is determined at interface level.
* USB class, subclass and protocol numbers should be setup before importing.
* Because windows vhci driver builds a device compatible id with those numbers.
*/
PAGEABLE auto init_vpdo(vpdo_dev_t &vpdo, const USB_CONFIGURATION_DESCRIPTOR &d)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo.actconfig); // first time initialization
	vpdo.actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePoolWithTag(PagedPool, d.wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo.actconfig) {
		RtlCopyMemory(vpdo.actconfig, &d, d.wTotalLength);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate %d bytes of memory", d.wTotalLength);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	return d.bNumInterfaces == 1 && !(vpdo.bDeviceClass || vpdo.bDeviceSubClass || vpdo.bDeviceProtocol) ?
		set_class_subclass_proto(vpdo) : STATUS_SUCCESS;
}

} // namespace


PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, vhci_pluginfo_t *pluginfo, ULONG inlen, FILE_OBJECT *fo)
{
	PAGED_CODE();

	if (inlen < sizeof(*pluginfo)) {
		Trace(TRACE_LEVEL_ERROR, "too small input length: %lld < %lld", inlen, sizeof(*pluginfo));
		return STATUS_INVALID_PARAMETER;
	}

	auto wTotalLength = pluginfo->dscr_conf.wTotalLength;

	if (inlen != sizeof(*pluginfo) + wTotalLength - sizeof(pluginfo->dscr_conf)) {
		Trace(TRACE_LEVEL_ERROR, "invalid pluginfo format: %lld != %lld", inlen, sizeof(*pluginfo) + wTotalLength - sizeof(pluginfo->dscr_conf));
		return STATUS_INVALID_PARAMETER;
	}

	auto devobj = vdev_create(vhci->Self->DriverObject, VDEV_VPDO);
	if (!devobj) {
		return STATUS_UNSUCCESSFUL;
	}

	auto vpdo = to_vpdo_or_null(devobj);

	vpdo->devid = pluginfo->devid;
	vpdo->parent = vhub_from_vhci(vhci);

	if (auto err = init_vpdo(*vpdo, pluginfo->dscr_dev)) {
		destroy_device(vpdo);
		return err;
	}
	
	if (auto err = init_vpdo(*vpdo, pluginfo->dscr_conf)) {
		destroy_device(vpdo);
		return err;
	}

	if (auto err = vhci_init_vpdo(*vpdo)) {
		destroy_device(vpdo);
		return err;
	}

	NT_ASSERT(vpdo->port); // was assigned
	pluginfo->port = static_cast<char>(vpdo->port);
	NT_ASSERT(pluginfo->port == vpdo->port);

	vpdo->SerialNumberUser = *pluginfo->wserial ? libdrv_strdupW(NonPagedPool, pluginfo->wserial) : nullptr;

	if (InterlockedCompareExchangePointer(&fo->FsContext, vpdo, nullptr)) {
		Trace(TRACE_LEVEL_INFORMATION, "You can't plugin again");
		destroy_device(vpdo);
		return STATUS_INVALID_PARAMETER;
	}

	vpdo->fo = fo;
	
	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhci->pdo, BusRelations);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhci_unplug_vpdo(vhci_dev_t *vhci, int port)
{
	PAGED_CODE();

	auto vhub = vhub_from_vhci(vhci);
	if (!vhub) {
		Trace(TRACE_LEVEL_INFORMATION, "vhub has gone");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port < 0) {
		Trace(TRACE_LEVEL_INFORMATION, "Plugging out all the devices");
		vhub_unplug_all_vpdo(vhub);
		return STATUS_SUCCESS;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, port)) {
		return vhub_unplug_vpdo(vpdo);
	}

	Trace(TRACE_LEVEL_ERROR, "Invalid or empty port %d", port);
	return STATUS_NO_SUCH_DEVICE;
}
