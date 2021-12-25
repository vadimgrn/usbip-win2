#include "devconf.h"

extern "C" {
#include <usbdlib.h>
}

USB_COMMON_DESCRIPTOR *dsc_find_next(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_COMMON_DESCRIPTOR *from, int type)
{
	USB_COMMON_DESCRIPTOR *start = dsc_next(from ? from : (USB_COMMON_DESCRIPTOR*)dsc_conf);
	NT_ASSERT(start > (USB_COMMON_DESCRIPTOR*)dsc_conf);

	return USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, type);
}

USB_INTERFACE_DESCRIPTOR *dsc_find_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num, UCHAR alt_setting)
{
	return USBD_ParseConfigurationDescriptorEx(dsc_conf, dsc_conf, intf_num, alt_setting, -1, -1, -1);
}

void *dsc_for_each_endpoint(
	USB_CONFIGURATION_DESCRIPTOR *dsc_conf,
	USB_INTERFACE_DESCRIPTOR *dsc_intf,
	dsc_for_each_ep_fn *func,
	void *data)
{
	USB_COMMON_DESCRIPTOR *cur = (USB_COMMON_DESCRIPTOR*)dsc_intf;

	for (int i = 0; i < dsc_intf->bNumEndpoints; ++i) {

		cur = dsc_find_next(dsc_conf, cur, USB_ENDPOINT_DESCRIPTOR_TYPE);
		if (!cur) {
			NT_ASSERT(!"endpoint expected");
			break;
		}

		if (func(i, (USB_ENDPOINT_DESCRIPTOR*)cur, data)) {
			return cur;
		}
	}

	return nullptr;
}

static bool match_epaddr(int i, USB_ENDPOINT_DESCRIPTOR *d, void *data)
{
	UNREFERENCED_PARAMETER(i);
	return d->bEndpointAddress == *(UCHAR*)data;
}

USB_ENDPOINT_DESCRIPTOR *dsc_find_intf_ep(
	USB_CONFIGURATION_DESCRIPTOR *dsc_conf,
	USB_INTERFACE_DESCRIPTOR *dsc_intf,
	UCHAR epaddr)
{
	return (USB_ENDPOINT_DESCRIPTOR*)dsc_for_each_endpoint(dsc_conf, dsc_intf, match_epaddr, &epaddr);
}

static bool match_ep(int i, USB_ENDPOINT_DESCRIPTOR *a, void *data)
{
	UNREFERENCED_PARAMETER(i);
	auto b = static_cast<USB_ENDPOINT_DESCRIPTOR*>(data);
	return a->bLength == b->bLength && RtlCompareMemory(a, b, a->bLength) == a->bLength;
}

USB_INTERFACE_DESCRIPTOR *dsc_find_intf_by_ep(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_ENDPOINT_DESCRIPTOR *dsc_ep)
{
	for (USB_INTERFACE_DESCRIPTOR *cur = nullptr; (cur = dsc_find_next_intf(dsc_conf, cur)) != nullptr; ) {
		if (dsc_for_each_endpoint(dsc_conf, cur, match_ep, dsc_ep)) {
			return cur;
		}
	}

	return nullptr;
}

/*
* bNumInterfaces is a count of unique interface numbers (altsettings are ignored).
* @return total number of interface descriptors
*/
ULONG dsc_conf_get_n_intfs(USB_CONFIGURATION_DESCRIPTOR *dsc_conf)
{
	ULONG n_intfs = 0;
	for (USB_INTERFACE_DESCRIPTOR *cur = nullptr; (cur = dsc_find_next_intf(dsc_conf, cur)) != nullptr; ++n_intfs);

	NT_ASSERT(n_intfs >= dsc_conf->bNumInterfaces);
	return n_intfs;
}

ULONG dsc_get_infs_length(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_INTERFACE_DESCRIPTOR *dsc_intf)
{
	NT_ASSERT((USB_COMMON_DESCRIPTOR*)dsc_conf < (USB_COMMON_DESCRIPTOR*)dsc_intf);
	ptrdiff_t infs_offset = (char*)dsc_intf - (char*)dsc_conf;

	UCHAR *conf_end = (UCHAR*)dsc_conf + dsc_conf->wTotalLength - infs_offset;
	return conf_end > (UCHAR*)dsc_intf ? USBD_GetInterfaceLength(dsc_intf, conf_end) : 0;
}

