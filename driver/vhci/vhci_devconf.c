#include "vhci_devconf.h"
#include "trace.h"
#include "vhci_devconf.tmh"

#include "vhci.h"
#include "usbip_vhci_api.h"
#include "devconf.h"

static __inline USBD_INTERFACE_INFORMATION* get_next_interface(USBD_INTERFACE_INFORMATION *iface)
{
	void *end = (char*)iface + iface->Length;
	return end;
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

struct init_ep_data
{
	USBD_PIPE_INFORMATION *pi;
	enum usb_device_speed speed;
};

static bool init_ep(int i, USB_ENDPOINT_DESCRIPTOR *d, void *data)
{
	struct init_ep_data *params = data;
	USBD_PIPE_INFORMATION *pi = params->pi + i;

	set_pipe(pi, d, params->speed);
	return false;
}

NTSTATUS setup_intf(USBD_INTERFACE_INFORMATION *intf, USB_CONFIGURATION_DESCRIPTOR *cfgd, enum usb_device_speed speed)
{
	if (intf->Length < sizeof(*intf) - sizeof(intf->Pipes)) { // can have zero pipes
		TraceError(TRACE_URB, "Interface length %d is too short", intf->Length);
		return STATUS_SUCCESS;
	}

	USB_INTERFACE_DESCRIPTOR *ifd = dsc_find_intf(cfgd, intf->InterfaceNumber, intf->AlternateSetting);
	if (!ifd) {
		TraceWarning(TRACE_IOCTL, "Can't find descriptor: InterfaceNumber %d, AlternateSetting %d",
					intf->InterfaceNumber, intf->AlternateSetting);

		return STATUS_INVALID_DEVICE_REQUEST;
	}

	intf->Class = ifd->bInterfaceClass;
	intf->SubClass = ifd->bInterfaceSubClass;
	intf->Protocol = ifd->bInterfaceProtocol;
	intf->InterfaceHandle = make_interface_handle(intf->InterfaceNumber, intf->AlternateSetting);
	intf->NumberOfPipes = ifd->bNumEndpoints;

	struct init_ep_data data = { intf->Pipes, speed };
	dsc_for_each_endpoint(cfgd, ifd, init_ep, &data);

	return STATUS_SUCCESS;
}

/*
 * An URB_FUNCTION_SELECT_CONFIGURATION URB consists of a _URB_SELECT_CONFIGURATION structure 
 * followed by a sequence of variable-length array of USBD_INTERFACE_INFORMATION structures, 
 * each element in the array for each unique interface number in the configuration. 
 */
NTSTATUS setup_config(
	USB_CONFIGURATION_DESCRIPTOR *cfgd, 
	USBD_INTERFACE_INFORMATION *iface, 
	const void *cfg_end, 
	enum usb_device_speed speed)
{
	for (int i = 0; i < cfgd->bNumInterfaces && (void*)iface < cfg_end; ++i, iface = get_next_interface(iface)) {
		
		NTSTATUS status = setup_intf(iface, cfgd, speed);
		if (status != STATUS_SUCCESS) {
			return status;
		}
	}

	return STATUS_SUCCESS;
}
