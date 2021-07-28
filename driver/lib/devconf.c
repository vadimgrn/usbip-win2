#include "devconf.h"
#include <usbdlib.h>

USB_INTERFACE_DESCRIPTOR *dsc_find_first_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf)
{
	return (USB_INTERFACE_DESCRIPTOR*)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, dsc_conf, USB_INTERFACE_DESCRIPTOR_TYPE);
}

USB_INTERFACE_DESCRIPTOR *dsc_find_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num, UCHAR alt_setting)
{
	return USBD_ParseConfigurationDescriptorEx(dsc_conf, dsc_conf, intf_num, alt_setting, -1, -1, -1);
}

static BOOLEAN intf_has_matched_ep(
	USB_CONFIGURATION_DESCRIPTOR* dsc_conf, 
	USB_INTERFACE_DESCRIPTOR *dsc_intf, 
	USB_ENDPOINT_DESCRIPTOR *dsc_ep)
{
	void *start = dsc_intf;

	for (int i = 0; i < dsc_intf->bNumEndpoints; ++i) {

		USB_ENDPOINT_DESCRIPTOR *dsc_ep_try = dsc_next_ep(dsc_conf, start);
		if (!dsc_ep_try) {
			break;
		}

		if (dsc_ep->bLength == dsc_ep_try->bLength) {
			if (RtlCompareMemory(dsc_ep, dsc_ep_try, dsc_ep->bLength) == dsc_ep->bLength)
				return TRUE;
		}

		start = dsc_ep_try;
	}

	return FALSE;
}

USB_INTERFACE_DESCRIPTOR *dsc_find_intf_by_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_ENDPOINT_DESCRIPTOR *dsc_ep)
{
	for (void *start = dsc_conf; start; ) {

		USB_INTERFACE_DESCRIPTOR *dsc_intf = (USB_INTERFACE_DESCRIPTOR*)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (!dsc_intf) {
			break;
		}

		if (intf_has_matched_ep(dsc_conf, dsc_intf, dsc_ep)) {
			return dsc_intf;
		}

		start = get_next_descr((USB_COMMON_DESCRIPTOR*)dsc_intf);
	}

	return NULL;
}

USB_ENDPOINT_DESCRIPTOR *dsc_find_intf_ep(
	USB_CONFIGURATION_DESCRIPTOR* dsc_conf, 
	USB_INTERFACE_DESCRIPTOR* dsc_intf, 
	UCHAR epaddr)
{
	void *start = dsc_intf;

	for (int i = 0; i < dsc_intf->bNumEndpoints; ++i) {
		
		USB_ENDPOINT_DESCRIPTOR *dsc_ep = dsc_next_ep(dsc_conf, start);
		if (!dsc_ep) {
			return NULL;
		}
		
		if (dsc_ep->bEndpointAddress == epaddr) {
			return dsc_ep;
		}

		start = dsc_ep;
	}

	return NULL;
}

USB_ENDPOINT_DESCRIPTOR *dsc_next_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_COMMON_DESCRIPTOR *start)
{
	if (start->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE) {
		start = get_next_descr(start);
	}

	start = USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_ENDPOINT_DESCRIPTOR_TYPE);
	return (USB_ENDPOINT_DESCRIPTOR*)start;
}

ULONG dsc_conf_get_n_intfs(USB_CONFIGURATION_DESCRIPTOR *dsc_conf)
{
	ULONG n_intfs = 0;

	for (void *start = dsc_conf; start; ++n_intfs) {
		USB_COMMON_DESCRIPTOR *desc = USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (desc) {
			start = get_next_descr(desc);
		} else {
			break;
		}
	}

	return n_intfs;
}
