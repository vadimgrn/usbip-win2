#include "devconf.h"
#include "trace.h"
#include "devconf.tmh"

#include "vhci.h"
#include "usbip_vhci_api.h"
#include "usbdsc.h"
#include "dbgcommon.h"

#include <ntstrsafe.h>

namespace
{

_IRQL_requires_max_(DISPATCH_LEVEL)
auto next_interface(const USBD_INTERFACE_INFORMATION *iface, const void *cfg_end)
{
	const void *next = (char*)iface + iface->Length;
	if (!cfg_end) {
		return (USBD_INTERFACE_INFORMATION*)next;
	}

	NT_ASSERT((void*)iface < cfg_end);
	return (USBD_INTERFACE_INFORMATION*)(next < cfg_end ? next : nullptr);
}

inline const void *get_configuration_end(const _URB_SELECT_CONFIGURATION *cfg)
{
	return (char*)cfg + cfg->Hdr.Length;
}

inline auto make_pipe_handle(UCHAR EndpointAddress, USBD_PIPE_TYPE PipeType, UCHAR Interval)
{
	UCHAR v[sizeof(USBD_PIPE_HANDLE)] = { EndpointAddress, Interval, static_cast<UCHAR>(PipeType) };
	NT_ASSERT(*(USBD_PIPE_HANDLE*)v);
	return *(USBD_PIPE_HANDLE*)v;
}

inline auto make_interface_handle(UCHAR ifnum, UCHAR altsetting)
{
	UCHAR v[sizeof(USBD_INTERFACE_HANDLE)] = { altsetting, ifnum, 1 }; // must be != 0
	return *(USBD_INTERFACE_HANDLE*)v; 
}

inline auto get_interface_altsettings(USBD_INTERFACE_HANDLE handle)
{
	auto v = (UCHAR*)&handle;
	return v[0]; 
}

inline auto get_interface_number(USBD_INTERFACE_HANDLE handle)
{
	auto v = (UCHAR*)&handle;
	return v[1]; 
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void set_pipe(_Out_ USBD_PIPE_INFORMATION &pipe, _In_ const USB_ENDPOINT_DESCRIPTOR &epd, _In_ usb_device_speed speed)
{
	pipe.MaximumPacketSize = epd.wMaxPacketSize;

	/* From usb_submit_urb in linux */
	if (pipe.PipeType == UsbdPipeTypeIsochronous && speed == USB_SPEED_HIGH) {
		USHORT	mult = 1 + ((pipe.MaximumPacketSize >> 11) & 0x03);
		pipe.MaximumPacketSize &= 0x7ff;
		pipe.MaximumPacketSize *= mult;
	}

	pipe.EndpointAddress = epd.bEndpointAddress;
	pipe.Interval = epd.bInterval;
	pipe.PipeType = static_cast<USBD_PIPE_TYPE>(epd.bmAttributes & USB_ENDPOINT_TYPE_MASK);

	pipe.PipeHandle = make_pipe_handle(epd.bEndpointAddress, pipe.PipeType, epd.bInterval);
	NT_ASSERT(pipe.PipeHandle);
	NT_ASSERT(is_endpoint_direction_in(pipe.PipeHandle) == (bool)USBD_PIPE_DIRECTION_IN(&pipe));

	pipe.MaximumTransferSize = 0; // is not used and does not contain valid data
	pipe.PipeFlags = 0; // USBD_PF_CHANGE_MAX_PACKET if override MaximumPacketSize
}

struct init_ep_data
{
	USBD_INTERFACE_INFORMATION &iface;
	usb_device_speed speed;
};

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(for_each_ep_fn)
NTSTATUS init_ep(int i, const USB_ENDPOINT_DESCRIPTOR &epd, void *data)
{
	auto &r = *static_cast<init_ep_data*>(data);
	auto cnt = r.iface.NumberOfPipes;
	
	if (ULONG(i) < cnt) {
		auto &pipe = r.iface.Pipes[i];
		set_pipe(pipe, epd, r.speed);
		return STATUS_SUCCESS;
	} else {
		Trace(TRACE_LEVEL_ERROR, "Endpoint index %d, NumberOfPipes %lu", i, cnt);
		return STATUS_INVALID_PARAMETER;
	}
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void interfaces_str(char *buf, size_t len, const USBD_INTERFACE_INFORMATION *r, int cnt, const void *cfg_end)
{
	auto st = STATUS_SUCCESS;

	for (int i = 0; i < cnt && st == STATUS_SUCCESS; ++i, r = next_interface(r, cfg_end)) {

		st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,
			"\nInterface(Length %d, InterfaceNumber %d, AlternateSetting %d, "
			"Class %#02hhx, SubClass %#02hhx, Protocol %#02hhx, InterfaceHandle %#Ix, NumberOfPipes %lu)", 
			r->Length, 
			r->InterfaceNumber,
			r->AlternateSetting,
			r->Class,
			r->SubClass,
			r->Protocol,
			(uintptr_t)r->InterfaceHandle,
			r->NumberOfPipes);

		for (ULONG j = 0; j < r->NumberOfPipes && st == STATUS_SUCCESS; ++j) {

			const USBD_PIPE_INFORMATION *p = r->Pipes + j;

			st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,
				"\nPipes[%lu](MaximumPacketSize %#x, EndpointAddress %#02hhx %s[%d], Interval %#hhx, %s, "
				"PipeHandle %#Ix, MaximumTransferSize %#lx, PipeFlags %#lx)",
				j,
				p->MaximumPacketSize,
				p->EndpointAddress,
				USB_ENDPOINT_DIRECTION_IN(p->EndpointAddress) ? "IN" : "OUT",
				p->EndpointAddress & USB_ENDPOINT_ADDRESS_MASK,
				p->Interval,
				usbd_pipe_type_str(p->PipeType),
				(uintptr_t)p->PipeHandle,
				p->MaximumTransferSize,
				p->PipeFlags);
		}
	}
}

} // namespace


_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS setup_intf(USBD_INTERFACE_INFORMATION *intf, usb_device_speed speed, USB_CONFIGURATION_DESCRIPTOR *cfgd)
{
	NT_ASSERT(cfgd);

	if (intf->Length < sizeof(*intf) - sizeof(intf->Pipes)) { // can have zero pipes
		Trace(TRACE_LEVEL_ERROR, "Interface length %d is too short", intf->Length);
		return STATUS_SUCCESS;
	}

	auto ifd = dsc_find_intf(cfgd, intf->InterfaceNumber, intf->AlternateSetting);
	if (!ifd) {
		Trace(TRACE_LEVEL_WARNING, "Can't find descriptor: InterfaceNumber %d, AlternateSetting %d",
					intf->InterfaceNumber, intf->AlternateSetting);

		return STATUS_INVALID_DEVICE_REQUEST;
	}

	intf->Class = ifd->bInterfaceClass;
	intf->SubClass = ifd->bInterfaceSubClass;
	intf->Protocol = ifd->bInterfaceProtocol;
	
	intf->InterfaceHandle = make_interface_handle(intf->InterfaceNumber, intf->AlternateSetting);
	NT_ASSERT(intf->InterfaceHandle);

	intf->NumberOfPipes = ifd->bNumEndpoints;
	init_ep_data data{ *intf, speed };

	return for_each_endpoint(cfgd, ifd, init_ep, &data);
}

/*
 * An URB_FUNCTION_SELECT_CONFIGURATION URB consists of a _URB_SELECT_CONFIGURATION structure 
 * followed by a sequence of variable-length array of USBD_INTERFACE_INFORMATION structures, 
 * each element in the array for each unique interface number in the configuration. 
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS setup_config(_URB_SELECT_CONFIGURATION *cfg, usb_device_speed speed)
{
	auto cd = cfg->ConfigurationDescriptor;
	NT_ASSERT(cd);

	auto iface = &cfg->Interface;
	auto cfg_end = get_configuration_end(cfg);

	for (int i = 0; i < cd->bNumInterfaces; ++i, iface = next_interface(iface, cfg_end)) {
		if (auto err = setup_intf(iface, speed, cd)) {
			return err;
		}
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_configuration_str(char *buf, size_t len, const _URB_SELECT_CONFIGURATION *cfg)
{
	auto cd = cfg->ConfigurationDescriptor;
	if (!cd) {
		auto st = RtlStringCbPrintfA(buf, len, "ConfigurationHandle %#Ix, ConfigurationDescriptor NULL (unconfigured)", 
							(uintptr_t)cfg->ConfigurationHandle);

		return st != STATUS_INVALID_PARAMETER ? buf : "select_configuration_str invalid parameter";
	}
	
	const char *result = buf;

	auto st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,
			"ConfigurationHandle %#Ix, "
			"ConfigurationDescriptor(bLength %d, bDescriptorType %d (must be %d), wTotalLength %hu, "
			"bNumInterfaces %d, bConfigurationValue %d, iConfiguration %d, bmAttributes %#x, MaxPower %d)",
			(uintptr_t)cfg->ConfigurationHandle,
			cd->bLength,
			cd->bDescriptorType,
			USB_CONFIGURATION_DESCRIPTOR_TYPE,
			cd->wTotalLength,
			cd->bNumInterfaces,
			cd->bConfigurationValue,
			cd->iConfiguration,
			cd->bmAttributes,
			cd->MaxPower);

	if (st == STATUS_SUCCESS) {
		auto cfg_end = get_configuration_end(cfg);
		interfaces_str(buf, len, &cfg->Interface, cd->bNumInterfaces, cfg_end);
	}

	return result && *result ? result : "select_configuration_str error";
}

_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_interface_str(char *buf, size_t len, const _URB_SELECT_INTERFACE *iface)
{
	const char *result = buf;
	auto st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,  
					"ConfigurationHandle %#Ix", (uintptr_t)iface->ConfigurationHandle);

	if (st == STATUS_SUCCESS) {
		interfaces_str(buf, len, &iface->Interface, 1, nullptr);
	}

	return result && *result ? result : "select_interface_str error";
}
