#pragma once

#include <ntddk.h>
#include <usbdi.h>

__inline USB_COMMON_DESCRIPTOR *get_next_descr(USB_COMMON_DESCRIPTOR *d)
{
	void *end = d ? (char*)d + d->bLength : NULL;
	return end;
}

USB_INTERFACE_DESCRIPTOR *dsc_find_first_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf);
USB_INTERFACE_DESCRIPTOR *dsc_find_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num, UCHAR alt_setting);
USB_INTERFACE_DESCRIPTOR *dsc_find_intf_by_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_ENDPOINT_DESCRIPTOR *dsc_ep);

USB_ENDPOINT_DESCRIPTOR *dsc_find_intf_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_INTERFACE_DESCRIPTOR *dsc_intf, UCHAR epaddr);
USB_ENDPOINT_DESCRIPTOR *dsc_next_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_COMMON_DESCRIPTOR *start);

ULONG dsc_conf_get_n_intfs(USB_CONFIGURATION_DESCRIPTOR *dsc_conf);
