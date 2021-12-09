#pragma once

#include <ntdef.h> 
#include <wdm.h>
#include <usbdi.h>

#include <stdbool.h>

#include "usbip_proto.h" 

NTSTATUS setup_config(struct _URB_SELECT_CONFIGURATION *cfg, enum usb_device_speed speed);
NTSTATUS setup_intf(USBD_INTERFACE_INFORMATION *intf_info, enum usb_device_speed speed, USB_CONFIGURATION_DESCRIPTOR *cfgd);

enum { 
	SELECT_CONFIGURATION_STR_BUFSZ = 1024, 
	SELECT_INTERFACE_STR_BUFSZ = SELECT_CONFIGURATION_STR_BUFSZ 
};

const char *select_configuration_str(char *buf, size_t len, const struct _URB_SELECT_CONFIGURATION *cfg);
const char *select_interface_str(char *buf, size_t len, const struct _URB_SELECT_INTERFACE *iface);

__inline const void *get_configuration_end(const struct _URB_SELECT_CONFIGURATION *cfg)
{
	return (char*)cfg + cfg->Hdr.Length;
}

__inline USBD_INTERFACE_INFORMATION *get_next_interface(const USBD_INTERFACE_INFORMATION *iface, const void *cfg_end)
{
	NT_ASSERT((void*)iface < cfg_end);
	void *next = (char*)iface + iface->Length;
	return next < cfg_end ? next : NULL;
}

__inline USBD_PIPE_HANDLE make_pipe_handle(UCHAR EndpointAddress, USBD_PIPE_TYPE PipeType, UCHAR Interval)
{
	UCHAR v[sizeof(USBD_PIPE_HANDLE)] = {EndpointAddress, Interval, PipeType};
	NT_ASSERT(*(USBD_PIPE_HANDLE*)v);
	return *(USBD_PIPE_HANDLE*)v;
}

/*
 * @return bEndpointAddress of endpoint descriptor 
 */
__inline UCHAR get_endpoint_address(USBD_PIPE_HANDLE handle)
{
	UCHAR *v = (UCHAR*)&handle;
	return v[0];
}

__inline UCHAR get_endpoint_interval(USBD_PIPE_HANDLE handle)
{
	UCHAR *v = (UCHAR*)&handle;
	return v[1];
}

__inline USBD_PIPE_TYPE get_endpoint_type(USBD_PIPE_HANDLE handle)
{
	UCHAR *v = (UCHAR*)&handle;
	return v[2];
}

__inline UCHAR get_endpoint_number(USBD_PIPE_HANDLE handle)
{
	UCHAR addr = get_endpoint_address(handle);
	return addr & USB_ENDPOINT_ADDRESS_MASK;
}

__inline bool is_endpoint_direction_in(USBD_PIPE_HANDLE handle)
{
	UCHAR addr = get_endpoint_address(handle);
	return USB_ENDPOINT_DIRECTION_IN(addr);
}

__inline bool is_endpoint_direction_out(USBD_PIPE_HANDLE handle)
{
	UCHAR addr = get_endpoint_address(handle);
	return USB_ENDPOINT_DIRECTION_OUT(addr);
}
