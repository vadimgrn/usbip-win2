#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <usbdi.h>

#include <stdbool.h>

__inline bool is_valid_dsc(const USB_DEVICE_DESCRIPTOR *d)
{
	NT_ASSERT(d);
	return  d->bLength == sizeof(*d) &&
		d->bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE;
}

__inline USB_COMMON_DESCRIPTOR *dsc_next(USB_COMMON_DESCRIPTOR *d)
{
	void *end = d ? (char*)d + d->bLength : NULL;
	return (USB_COMMON_DESCRIPTOR*)end;
}

USB_COMMON_DESCRIPTOR *dsc_find_next(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_COMMON_DESCRIPTOR *from, int type);

__inline USB_INTERFACE_DESCRIPTOR *dsc_find_next_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_INTERFACE_DESCRIPTOR *from)
{
	return (USB_INTERFACE_DESCRIPTOR*)dsc_find_next(dsc_conf, (USB_COMMON_DESCRIPTOR*)from, USB_INTERFACE_DESCRIPTOR_TYPE);
}

typedef bool dsc_for_each_ep_fn(int, USB_ENDPOINT_DESCRIPTOR*, void*);

void *dsc_for_each_endpoint(
	USB_CONFIGURATION_DESCRIPTOR *dsc_conf, 
	USB_INTERFACE_DESCRIPTOR *dsc_intf, 
	dsc_for_each_ep_fn *func, 
	void *data);

USB_INTERFACE_DESCRIPTOR *dsc_find_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num, UCHAR alt_setting);
int get_intf_num_altsetting(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num);

#ifdef __cplusplus
}
#endif
