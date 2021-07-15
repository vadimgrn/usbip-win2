#include "vhci.h"

#include "vhci_devconf.h"
#include "usbip_vhci_api.h"
#include "devconf.h"

#define NEXT_USBD_INTERFACE_INFO(info_intf)	(USBD_INTERFACE_INFORMATION *)((PUINT8)(info_intf + 1) - \
	(1 * sizeof(USBD_PIPE_INFORMATION)) + (info_intf->NumberOfPipes * sizeof(USBD_PIPE_INFORMATION)));

#define MAKE_PIPE(ep, type, interval) ((USBD_PIPE_HANDLE)((ep) | ((interval) << 8) | ((type) << 16)))
#define TO_INTF_HANDLE(intf_num, altsetting)	((USBD_INTERFACE_HANDLE)((intf_num << 8) + altsetting))
#define TO_INTF_NUM(handle)		(UCHAR)(((UINT_PTR)(handle)) >> 8)
#define TO_INTF_ALTSETTING(handle)	(UCHAR)((UINT_PTR)(handle) & 0xff)

#ifdef DBG

static const char *
dbg_pipe(PUSBD_PIPE_INFORMATION pipe)
{
	static char	buf[512];

	libdrv_snprintf(buf, 512, "addr:%02x intv:%d typ:%d mps:%d mts:%d flags:%x",
		pipe->EndpointAddress, pipe->Interval, pipe->PipeType, pipe->PipeFlags,
		pipe->MaximumPacketSize, pipe->MaximumTransferSize, pipe->PipeFlags);
	return buf;
}

#endif

static void
set_pipe(PUSBD_PIPE_INFORMATION pipe, PUSB_ENDPOINT_DESCRIPTOR ep_desc, enum usb_device_speed speed)
{
	pipe->MaximumPacketSize = ep_desc->wMaxPacketSize;
	pipe->EndpointAddress = ep_desc->bEndpointAddress;
	pipe->Interval = ep_desc->bInterval;
	pipe->PipeType = ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK;
	/* From usb_submit_urb in linux */
	if (pipe->PipeType == USB_ENDPOINT_TYPE_ISOCHRONOUS && speed == USB_SPEED_HIGH) {
		USHORT	mult = 1 + ((pipe->MaximumPacketSize >> 11) & 0x03);
		pipe->MaximumPacketSize &= 0x7ff;
		pipe->MaximumPacketSize *= mult;
	}
	pipe->PipeHandle = MAKE_PIPE(ep_desc->bEndpointAddress, pipe->PipeType, ep_desc->bInterval);
}

static NTSTATUS
setup_endpoints(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PUSB_INTERFACE_DESCRIPTOR dsc_intf, enum usb_device_speed speed)
{
	PVOID	start = dsc_intf;
	ULONG	n_pipes_setup;
	unsigned int	i;

	n_pipes_setup = (intf->Length - sizeof(USBD_INTERFACE_INFORMATION)) / sizeof(USBD_PIPE_INFORMATION) + 1;
	if (n_pipes_setup < intf->NumberOfPipes) {
		DBGW(DBG_URB, "insufficient interface information size: %u < %u\n", n_pipes_setup, intf->NumberOfPipes);
	}
	else {
		n_pipes_setup = intf->NumberOfPipes;
	}

	for (i = 0; i < n_pipes_setup; i++) {
		PUSB_ENDPOINT_DESCRIPTOR	dsc_ep;

		dsc_ep = dsc_next_ep(dsc_conf, start);
		if (dsc_ep == NULL) {
			DBGW(DBG_IOCTL, "no ep desc\n");
			return FALSE;
		}

		set_pipe(&intf->Pipes[i], dsc_ep, speed);
		DBGI(DBG_IOCTL, "ep setup[%u]: %s\n", i, dbg_pipe(&intf->Pipes[i]));
		start = dsc_ep;

	}
	return TRUE;
}

NTSTATUS
setup_intf(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, enum usb_device_speed speed)
{
	PUSB_INTERFACE_DESCRIPTOR	dsc_intf;

	if (sizeof(USBD_INTERFACE_INFORMATION) - sizeof(USBD_PIPE_INFORMATION) > intf->Length) {
		DBGE(DBG_URB, "insufficient interface information size?\n");
		///TODO: need to check
		return STATUS_SUCCESS;
	}

	dsc_intf = dsc_find_intf(dsc_conf, intf->InterfaceNumber, intf->AlternateSetting);
	if (dsc_intf == NULL) {
		DBGW(DBG_IOCTL, "no interface desc\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	intf->Class = dsc_intf->bInterfaceClass;
	intf->SubClass = dsc_intf->bInterfaceSubClass;
	intf->Protocol = dsc_intf->bInterfaceProtocol;
	intf->InterfaceHandle = TO_INTF_HANDLE(intf->InterfaceNumber, intf->AlternateSetting);
	intf->NumberOfPipes = dsc_intf->bNumEndpoints;

	if (!setup_endpoints(intf, dsc_conf, dsc_intf, speed))
		return STATUS_INVALID_DEVICE_REQUEST;
	return STATUS_SUCCESS;
}

NTSTATUS
setup_config(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PUSBD_INTERFACE_INFORMATION info_intf, PVOID end_info_intf, enum usb_device_speed speed)
{
	unsigned int	i;

	for (i = 0; i < dsc_conf->bNumInterfaces; i++) {
		NTSTATUS	status;

		if ((status = setup_intf(info_intf, dsc_conf, speed)) != STATUS_SUCCESS)
			return status;

		info_intf = NEXT_USBD_INTERFACE_INFO(info_intf);
		/* urb_selc may have less info_intf than bNumInterfaces in conf desc */
		if ((PVOID)info_intf >= end_info_intf)
			break;
	}

	/* it seems we must return now */
	return STATUS_SUCCESS;
}
