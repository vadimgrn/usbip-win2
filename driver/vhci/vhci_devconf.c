#include "vhci_devconf.h"
#include "vhci.h"
#include "usbip_vhci_api.h"
#include "devconf.h"

static __inline USBD_INTERFACE_INFORMATION* get_next_interface(USBD_INTERFACE_INFORMATION *iface)
{
	char *end = (char*)iface + iface->Length;
	return (USBD_INTERFACE_INFORMATION*)end;
}

static __inline USBD_INTERFACE_HANDLE make_interface_handle(UCHAR ifnum, UCHAR altsetting)
{
	UCHAR v[sizeof(USBD_INTERFACE_HANDLE)] = { altsetting, ifnum };
	return *(USBD_INTERFACE_HANDLE*)v; 
}

static __inline UCHAR get_interface_altsettings(USBD_INTERFACE_HANDLE handle)
{
	UCHAR *v = (UCHAR*)&handle;
	return v[0]; 
}

static __inline UCHAR get_interface_number(USBD_INTERFACE_HANDLE handle)
{
	UCHAR *v = (UCHAR*)&handle;
	return v[1]; 
}

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

static void set_pipe(USBD_PIPE_INFORMATION *pipe, USB_ENDPOINT_DESCRIPTOR *ep_desc, enum usb_device_speed speed)
{
	pipe->MaximumPacketSize = ep_desc->wMaxPacketSize;

	/* From usb_submit_urb in linux */
	if (pipe->PipeType == UsbdPipeTypeIsochronous && speed == USB_SPEED_HIGH) {
		USHORT	mult = 1 + ((pipe->MaximumPacketSize >> 11) & 0x03);
		pipe->MaximumPacketSize &= 0x7ff;
		pipe->MaximumPacketSize *= mult;
	}
	
	pipe->EndpointAddress = ep_desc->bEndpointAddress;
	pipe->Interval = ep_desc->bInterval;
	pipe->PipeType = ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK;
	pipe->PipeHandle = make_pipe_handle(ep_desc->bEndpointAddress, pipe->PipeType, ep_desc->bInterval);
	pipe->MaximumTransferSize = 0; // is not used and does not contain valid data
	pipe->PipeFlags = 0;
}

static NTSTATUS setup_endpoints(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PUSB_INTERFACE_DESCRIPTOR dsc_intf, enum usb_device_speed speed)
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

NTSTATUS setup_intf(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, enum usb_device_speed speed)
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
	intf->InterfaceHandle = make_interface_handle(intf->InterfaceNumber, intf->AlternateSetting);
	intf->NumberOfPipes = dsc_intf->bNumEndpoints;

	if (!setup_endpoints(intf, dsc_conf, dsc_intf, speed))
		return STATUS_INVALID_DEVICE_REQUEST;
	return STATUS_SUCCESS;
}

NTSTATUS setup_config(
	USB_CONFIGURATION_DESCRIPTOR *dsc_conf, 
	USBD_INTERFACE_INFORMATION *info_intf, 
	void *intf_end, 
	enum usb_device_speed speed)
{
	for (int i = 0; i < dsc_conf->bNumInterfaces; ++i) {
		
		NTSTATUS status = setup_intf(info_intf, dsc_conf, speed);
		if (status != STATUS_SUCCESS) {
			return status;
		}

		info_intf = get_next_interface(info_intf);
	
		/* urb_selc may have less info_intf than bNumInterfaces in conf desc */
		if ((void*)info_intf >= intf_end) {
			break;
		}
	}

	return STATUS_SUCCESS;
}
