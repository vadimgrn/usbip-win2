#include "usbdsc.h"

extern "C" {
#include <usbdlib.h>
}

namespace
{

inline auto get_start(USB_CONFIGURATION_DESCRIPTOR *cfg, USB_COMMON_DESCRIPTOR *prev)
{
	NT_ASSERT(cfg);

	auto start = prev ? usbdlib::next_descr(prev) : static_cast<void*>(cfg);
	NT_ASSERT(start >= cfg);

	return start;
}

} // namespace


USB_COMMON_DESCRIPTOR *usbdlib::find_next_descr(
USB_CONFIGURATION_DESCRIPTOR *cfg, LONG type, USB_COMMON_DESCRIPTOR *prev)
{
	auto start = get_start(cfg, prev);
	return USBD_ParseDescriptors(cfg, cfg->wTotalLength, start, type);
}

USB_INTERFACE_DESCRIPTOR* usbdlib::find_next_intf(
	USB_CONFIGURATION_DESCRIPTOR *cfg, USB_INTERFACE_DESCRIPTOR *prev, 
	LONG intf_num, LONG alt_setting, LONG _class, LONG subclass, LONG proto)
{
	auto start = get_start(cfg, reinterpret_cast<USB_COMMON_DESCRIPTOR*>(prev));
	return USBD_ParseConfigurationDescriptorEx(cfg, start, intf_num, alt_setting, _class, subclass, proto);
}

/*
 * @return number of alternate settings for given interface
 */
int usbdlib::get_intf_num_altsetting(USB_CONFIGURATION_DESCRIPTOR *cfg, LONG intf_num)
{
	int cnt = 0;
	for (USB_INTERFACE_DESCRIPTOR *cur{}; bool(cur = find_next_intf(cfg, cur, intf_num)); ++cnt);
	return cnt;
}

NTSTATUS usbdlib::for_each_intf(_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, for_each_iface_fn func, void *data)
{
	int cnt = 0;

	for (USB_COMMON_DESCRIPTOR *cur{}; bool(cur = find_next_descr(cfg, USB_INTERFACE_DESCRIPTOR_TYPE, cur)); ++cnt) {
		if (auto err = func(reinterpret_cast<USB_INTERFACE_DESCRIPTOR&>(*cur), data)) {
			return err;
		}
	}

	auto ret = cnt >= cfg->bNumInterfaces ? STATUS_SUCCESS : STATUS_NO_MORE_MATCHES;
	NT_ASSERT(!ret);
	return ret;
}

NTSTATUS usbdlib::for_each_endp(
	USB_CONFIGURATION_DESCRIPTOR *cfg, const USB_INTERFACE_DESCRIPTOR *iface, for_each_ep_fn func, void *data)
{
	auto cur = (USB_COMMON_DESCRIPTOR*)iface;

	for (int i = 0; i < iface->bNumEndpoints; ++i) {

		cur = find_next_descr(cfg, USB_ENDPOINT_DESCRIPTOR_TYPE, cur);
		if (!cur) {
			NT_ASSERT(!"Endpoint not found");
			return STATUS_NO_MORE_MATCHES;
		}

		if (auto err = func(i, *reinterpret_cast<USB_ENDPOINT_DESCRIPTOR*>(cur), data)) {
			return err;
		}
	}

	return STATUS_SUCCESS;
}

bool usbdlib::is_valid(const USB_DEVICE_DESCRIPTOR &d)
{
        return  d.bLength == sizeof(d) && 
		d.bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE;
}

bool usbdlib::is_valid(const USB_CONFIGURATION_DESCRIPTOR &d)
{
        return  d.bLength == sizeof(d) &&
                d.bDescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE &&
                d.wTotalLength > d.bLength;
}

bool usbdlib::is_valid(const USB_STRING_DESCRIPTOR &d)
{
	return  d.bLength >= sizeof(USB_COMMON_DESCRIPTOR) && // string length can be zero
		d.bDescriptorType == USB_STRING_DESCRIPTOR_TYPE;
}

bool usbdlib::is_valid(const USB_OS_STRING_DESCRIPTOR &d)
{
	return  d.bLength == sizeof(d) && 
		d.bDescriptorType == USB_STRING_DESCRIPTOR_TYPE && 
		RtlEqualMemory(d.Signature, L"MSFT100", sizeof(d.Signature));
}
