#pragma once

#include <ntddk.h>
#include <usbdi.h>

#include <stdbool.h>

__inline USB_COMMON_DESCRIPTOR *dsc_next(USB_COMMON_DESCRIPTOR *d)
{
	void *end = d ? (char*)d + d->bLength : NULL;
	return end;
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
USB_INTERFACE_DESCRIPTOR *dsc_find_intf_by_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_ENDPOINT_DESCRIPTOR *dsc_ep);

USB_ENDPOINT_DESCRIPTOR *dsc_find_intf_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_INTERFACE_DESCRIPTOR *dsc_intf, UCHAR epaddr);

ULONG dsc_get_infs_length(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_INTERFACE_DESCRIPTOR *dsc_intf);
ULONG dsc_conf_get_n_intfs(USB_CONFIGURATION_DESCRIPTOR *dsc_conf);
