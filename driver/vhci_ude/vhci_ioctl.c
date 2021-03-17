#include "vhci_driver.h"
#include "vhci_ioctl.tmh"

#include "usbip_vhci_api.h"

NTSTATUS
plugin_vusb(pctx_vhci_t vhci, WDFREQUEST req, pvhci_pluginfo_t pluginfo);

static VOID
get_ports_status(pctx_vhci_t vhci, ioctl_usbip_vhci_get_ports_status *ports_status)
{
	ULONG	i;

	TRD(IOCTL, "Enter\n");

	RtlZeroMemory(ports_status, sizeof(ioctl_usbip_vhci_get_ports_status));

	WdfSpinLockAcquire(vhci->spin_lock);

	for (i = 0; i != vhci->n_max_ports; i++) {
		pctx_vusb_t	vusb = vhci->vusbs[i];
		if (vusb != NULL) {
			ports_status->port_status[i] = 1;
		}
	}

	WdfSpinLockRelease(vhci->spin_lock);

	ports_status->n_max_ports = (UCHAR)vhci->n_max_ports;

	TRD(IOCTL, "Leave\n");
}

static NTSTATUS
ioctl_get_ports_status(WDFQUEUE queue, WDFREQUEST req)
{
	pctx_vhci_t	vhci;
	ioctl_usbip_vhci_get_ports_status	*ports_status;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req, sizeof(ioctl_usbip_vhci_get_ports_status), &ports_status, NULL);
	if (NT_ERROR(status))
		return status;

	vhci = *TO_PVHCI(queue);

	get_ports_status(vhci, ports_status);
	WdfRequestSetInformation(req, sizeof(ioctl_usbip_vhci_get_ports_status));

	return STATUS_SUCCESS;
}

static VOID
get_imported_devices(pctx_vhci_t vhci, pioctl_usbip_vhci_imported_dev_t idevs, ULONG n_idevs_max)
{
	pioctl_usbip_vhci_imported_dev_t	idev = idevs;
	ULONG	n_idevs = 0;
	ULONG	i;

	TRD(IOCTL, "Enter\n");

	WdfSpinLockAcquire(vhci->spin_lock);

	for (i = 0; i != vhci->n_max_ports && n_idevs < n_idevs_max - 1; i++) {
		pctx_vusb_t	vusb = vhci->vusbs[i];
		if (VUSB_IS_VALID(vusb)) {
			idev->port = (CHAR)i;
			idev->status = 2; /* SDEV_ST_USED */;
			idev->vendor = vusb->id_vendor;
			idev->product = vusb->id_product;
			idev->speed = (UCHAR)vusb->dev_speed;
			idev++;
		}
	}

	WdfSpinLockRelease(vhci->spin_lock);

	idev->port = 0xff; /* end of mark */

	TRD(IOCTL, "Leave\n");
}

static NTSTATUS
ioctl_get_imported_devices(WDFQUEUE queue, WDFREQUEST req, size_t outlen)
{
	pctx_vhci_t	vhci;
	pioctl_usbip_vhci_imported_dev_t	idevs;
	ULONG	n_idevs_max;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req, outlen, &idevs, NULL);
	if (NT_ERROR(status))
		return status;

	n_idevs_max = (ULONG)(outlen / sizeof(ioctl_usbip_vhci_imported_dev));
	if (n_idevs_max == 0)
		return STATUS_INVALID_PARAMETER;

	vhci = *TO_PVHCI(queue);

	get_imported_devices(vhci, idevs, n_idevs_max);
	WdfRequestSetInformation(req, outlen);

	return STATUS_SUCCESS;
}

static NTSTATUS
ioctl_plugin_vusb(WDFQUEUE queue, WDFREQUEST req, size_t inlen)
{
	pctx_vhci_t	vhci;
	pvhci_pluginfo_t	pluginfo;
	PUSHORT		pdscr_fullsize;
	size_t		len;
	NTSTATUS	status;

	if (inlen < sizeof(vhci_pluginfo_t)) {
		TRE(IOCTL, "too small input length: %lld < %lld", inlen, sizeof(vhci_pluginfo_t));
		return STATUS_INVALID_PARAMETER;
	}
	status = WdfRequestRetrieveInputBuffer(req, sizeof(vhci_pluginfo_t), &pluginfo, &len);
	if (NT_ERROR(status)) {
		TRE(IOCTL, "failed to get pluginfo buffer: %!STATUS!", status);
		return status;
	}
	pdscr_fullsize = (PUSHORT)pluginfo->dscr_conf + 1;
	if (len != sizeof(vhci_pluginfo_t) + *pdscr_fullsize - 9) {
		TRE(IOCTL, "invalid pluginfo format: %lld != %lld", len, sizeof(vhci_pluginfo_t) + *pdscr_fullsize - 9);
		return STATUS_INVALID_PARAMETER;
	}
	vhci = *TO_PVHCI(queue);
	if (pluginfo->port < 0 || (ULONG)pluginfo->port >= vhci->n_max_ports)
		return STATUS_INVALID_PARAMETER;
	return plugin_vusb(vhci, req, pluginfo);
}

static NTSTATUS
ioctl_plugout_vusb(WDFQUEUE queue, WDFREQUEST req, size_t inlen)
{
	pvhci_unpluginfo_t	unpluginfo;
	pctx_vhci_t	vhci;
	CHAR		port;
	NTSTATUS	status;

	if (inlen != sizeof(ioctl_usbip_vhci_unplug)) {
		TRE(IOCTL, "invalid unplug input size: %lld < %lld", inlen, sizeof(ioctl_usbip_vhci_unplug));
		return STATUS_INVALID_PARAMETER;
	}

	status = WdfRequestRetrieveInputBuffer(req, sizeof(ioctl_usbip_vhci_unplug), &unpluginfo, NULL);
	if (NT_ERROR(status)) {
		TRE(IOCTL, "failed to get unplug buffer: %!STATUS!", status);
		return status;
	}

	port = unpluginfo->addr;
	vhci = *TO_PVHCI(queue);
	if (port >= (CHAR)vhci->n_max_ports)
		return STATUS_INVALID_PARAMETER;

	return plugout_vusb(vhci, port);
}

static NTSTATUS
ioctl_shutdown_vusb(WDFQUEUE queue, WDFREQUEST req)
{
	pctx_vhci_t	vhci;
	pctx_vusb_t	vusb;
	NTSTATUS	status;

	vusb = get_vusb_by_req(req);
	if (vusb == NULL) {
		/* already detached */
		return STATUS_SUCCESS;
	}

	vhci = *TO_PVHCI(queue);

	status = plugout_vusb(vhci, (CHAR)vusb->port);
	put_vusb(vusb);

	return status;
}

VOID
io_device_control(_In_ WDFQUEUE queue, _In_ WDFREQUEST req,
	_In_ size_t outlen, _In_ size_t inlen, _In_ ULONG ioctl_code)
{
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	UNREFERENCED_PARAMETER(outlen);

	TRD(IOCTL, "Enter: %!IOCTL!", ioctl_code);

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		status = ioctl_get_ports_status(queue, req);
		break;
	case IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES:
		status = ioctl_get_imported_devices(queue, req, outlen);
		break;
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		status = ioctl_plugin_vusb(queue, req, inlen);
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		status = ioctl_plugout_vusb(queue, req, inlen);
		break;
	case IOCTL_USBIP_VHCI_SHUTDOWN_HARDWARE:
		status = ioctl_shutdown_vusb(queue, req);
		break;
	default:
		if (UdecxWdfDeviceTryHandleUserIoctl((*TO_PVHCI(queue))->hdev, req)) {
			TRD(IOCTL, "Leave: handled by Udecx");
			return;
		}
		TRE(IOCTL, "unhandled IOCTL: %!IOCTL!", ioctl_code);
		break;
	}

	WdfRequestComplete(req, status);

	TRD(IOCTL, "Leave: %!STATUS!", status);
}
