#include "vhci_driver.h"
#include "vhci_vusb.tmh"

pctx_vusb_t
get_vusb(pctx_vhci_t vhci, ULONG port)
{
	pctx_vusb_t	vusb;

	WdfSpinLockAcquire(vhci->spin_lock);

	vusb = vhci->vusbs[port];
	if (vusb == NULL || vusb->invalid) {
		WdfSpinLockRelease(vhci->spin_lock);
		return NULL;
	}
	vusb->refcnt++;

	WdfSpinLockRelease(vhci->spin_lock);

	return vusb;
}

pctx_vusb_t
get_vusb_by_req(WDFREQUEST req)
{
	pctx_safe_vusb_t	svusb;

	svusb = TO_SAFE_VUSB_FROM_REQ(req);
	return get_vusb(svusb->vhci, svusb->port);
}

void
put_vusb(pctx_vusb_t vusb)
{
	pctx_vhci_t	vhci = vusb->vhci;

	WdfSpinLockAcquire(vhci->spin_lock);

	ASSERT(vusb->refcnt > 0);
	vusb->refcnt--;
	if (vusb->refcnt == 0 && vusb->invalid) {
		NTSTATUS	status;

		vhci->vusbs[vusb->port] = NULL;
		vhci->n_used_ports--;
		WdfSpinLockRelease(vhci->spin_lock);

		status = UdecxUsbDevicePlugOutAndDelete(vusb->ude_usbdev);
		if (NT_ERROR(status)) {
			TRD(VUSB, "failed to plug out: %!STATUS!", status);
		}
	}
	else {
		WdfSpinLockRelease(vhci->spin_lock);
	}
}

static VOID
async_put_vusb(WDFWORKITEM workitem)
{
	pctx_vusb_t	vusb = *TO_PVUSB(workitem);

	TRD(VUSB, "async put vusb");
	put_vusb(vusb);
	WdfObjectDelete(workitem);
}

void
put_vusb_passively(pctx_vusb_t vusb)
{
	WDFWORKITEM	workitem;
	WDF_WORKITEM_CONFIG	conf;
	WDF_OBJECT_ATTRIBUTES	attrs;
	NTSTATUS	status;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, pctx_vusb_t);
	attrs.ParentObject = vusb->ude_usbdev;
	WDF_WORKITEM_CONFIG_INIT(&conf, async_put_vusb);

	status = WdfWorkItemCreate(&conf, &attrs, &workitem);
	if (NT_ERROR(status)) {
		TRW(VUSB, "failed to create a workitem: %!STATUS!", status);
	}
	else {
		*TO_PVUSB(workitem) = vusb;
		WdfWorkItemEnqueue(workitem);
	}
}