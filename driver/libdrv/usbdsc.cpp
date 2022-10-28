#include "usbdsc.h"
#include "ch9.h"

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
	NT_ASSERT(start <= reinterpret_cast<char*>(cfg) + cfg->wTotalLength);

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

/*
 * For each pair bInterfaceNumber/bAlternateSetting.
 */
NTSTATUS usbdlib::for_each_intf_alt(_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, for_each_intf_alt_fn func, void *data)
{
	int cnt = 0;

	for (USB_COMMON_DESCRIPTOR *cur{}; bool(cur = find_next_descr(cfg, USB_INTERFACE_DESCRIPTOR_TYPE, cur)); ++cnt) {
		auto ifd = reinterpret_cast<USB_INTERFACE_DESCRIPTOR*>(cur);
		if (auto err = func(*ifd, data)) {
			return err;
		}
	}

	auto ret = cnt >= cfg->bNumInterfaces ? STATUS_SUCCESS : STATUS_NO_MORE_MATCHES;
	NT_ASSERT(!ret);

	return ret;
}

NTSTATUS usbdlib::for_each_endp(
	USB_CONFIGURATION_DESCRIPTOR *cfg, USB_INTERFACE_DESCRIPTOR *ifd, for_each_ep_fn func, void *data)
{
	auto cur = (USB_COMMON_DESCRIPTOR*)ifd;

	for (int i = 0; i < ifd->bNumEndpoints; ++i) {

		cur = find_next_descr(cfg, USB_ENDPOINT_DESCRIPTOR_TYPE, cur);
		if (!cur) {
			NT_ASSERT(!"Endpoint not found");
			return STATUS_NO_MORE_MATCHES;
		}
		
		auto epd = reinterpret_cast<USB_ENDPOINT_DESCRIPTOR*>(cur);
		if (auto err = func(i, *epd, data)) {
			return err;
		}
	}

	return STATUS_SUCCESS;
}

USB_INTERFACE_DESCRIPTOR* usbdlib::find_intf(USB_CONFIGURATION_DESCRIPTOR *cfg, const USB_ENDPOINT_DESCRIPTOR &epd)
{
	struct Context
	{
		USB_CONFIGURATION_DESCRIPTOR *cfg;
		const USB_ENDPOINT_DESCRIPTOR &epd;
		USB_INTERFACE_DESCRIPTOR *ifd;
	} ctx {cfg, epd};

	auto f = [] (auto &ifd, auto ctx)
	{
		auto args = reinterpret_cast<Context*>(ctx);
		args->ifd = &ifd;

		auto f = [] (auto, auto &epd, auto ctx)
		{
			auto args = reinterpret_cast<Context*>(ctx);
			return epd == args->epd ? STATUS_PENDING : STATUS_SUCCESS;
		};

		return for_each_endp(args->cfg, &ifd, f, ctx);
	};

	auto ret = for_each_intf_alt(cfg, f, &ctx);
	return ret == STATUS_PENDING ? ctx.ifd : nullptr;
}

bool usbdlib::is_valid(const USB_OS_STRING_DESCRIPTOR &d)
{
	return  d.bLength == sizeof(d) && 
		d.bDescriptorType == USB_STRING_DESCRIPTOR_TYPE && 
		RtlEqualMemory(d.Signature, L"MSFT100", sizeof(d.Signature));
}

/*
 * Enumeration of USB Composite Devices.
 * 
 * The bus driver also reports a compatible identifier (ID) of USB\COMPOSITE,
 * if the device meets the following requirements:
 * 1.The device class field of the device descriptor (bDeviceClass) must contain a value of zero,
 *   or the class (bDeviceClass), bDeviceSubClass, and bDeviceProtocol fields
 *   of the device descriptor must have the values 0xEF, 0x02 and 0x01 respectively, as explained
 *   in USB Interface Association Descriptor.
 * 2.The device must have multiple interfaces.
 * 3.The device must have a single configuration.
 *
 * The bus driver checks the bDeviceClass, bDeviceSubClass and bDeviceProtocol fields of the device descriptor.  
 * If these fields are zero, the device is a composite device, and the bus driver reports an extra compatible
 * identifier (ID) of USB\COMPOSITE for the PDO.
 * 
 * P.S. FIXME: zero class/sub/proto matches flash drives which are not composite devices, so it is not used.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool usbdlib::is_composite(_In_ const USB_DEVICE_DESCRIPTOR &dd, _In_ const USB_CONFIGURATION_DESCRIPTOR &cd)
{
	NT_ASSERT(is_valid(dd));
	NT_ASSERT(is_valid(cd));

	if (!(dd.bNumConfigurations == 1 && cd.bNumInterfaces > 1)) {
		return false;
	}

	return dd.bDeviceClass == USB_DEVICE_CLASS_RESERVED || // generic composite device
	      (dd.bDeviceClass == USB_DEVICE_CLASS_MISCELLANEOUS && // 0xEF
	       dd.bDeviceSubClass == 0x02 && // common class
	       dd.bDeviceProtocol == 0x01); // IAD composite device
}

