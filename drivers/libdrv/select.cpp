/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "select.h"
#include "codeseg.h"
#include "dbgcommon.h"

#include <ntstrsafe.h>

namespace
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto next_interface(_In_ const USBD_INTERFACE_INFORMATION *iface, _In_opt_ const void *cfg_end)
{
	const void *next = (char*)iface + iface->Length;
	if (!cfg_end) {
		return (USBD_INTERFACE_INFORMATION*)next;
	}

	NT_ASSERT((void*)iface < cfg_end);
	return (USBD_INTERFACE_INFORMATION*)(next < cfg_end ? next : nullptr);
}

inline const void *get_configuration_end(_In_ const _URB_SELECT_CONFIGURATION *cfg)
{
	return (char*)cfg + cfg->Hdr.Length;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void interfaces_str(
	_In_ char *buf, _In_ size_t len, _In_ const USBD_INTERFACE_INFORMATION *r, _In_ int cnt, 
	_In_opt_ const void *cfg_end)
{
	auto st = STATUS_SUCCESS;

	for (int i = 0; i < cnt && !st; ++i, r = next_interface(r, cfg_end)) {

		st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,
			"\nInterface(Length %d, InterfaceNumber %d, AlternateSetting %d, "
			"Class %#02hhx, SubClass %#02hhx, Protocol %#02hhx, InterfaceHandle %04x, NumberOfPipes %lu)", 
			r->Length, 
			r->InterfaceNumber,
			r->AlternateSetting,
			r->Class,
			r->SubClass,
			r->Protocol,
			ptr04x(r->InterfaceHandle),
			r->NumberOfPipes);

		for (ULONG j = 0; j < r->NumberOfPipes && !st; ++j) {

			auto &p = r->Pipes[j];

			st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,
				"\nPipes[%lu](MaximumPacketSize %#x, EndpointAddress %#02hhx %s[%d], Interval %#hhx, %s, "
				"PipeHandle %04x, MaximumTransferSize %#lx, PipeFlags %#lx)",
				j,
				p.MaximumPacketSize,
				p.EndpointAddress,
				USB_ENDPOINT_DIRECTION_IN(p.EndpointAddress) ? "IN" : "OUT",
				p.EndpointAddress & USB_ENDPOINT_ADDRESS_MASK,
				p.Interval,
				usbd_pipe_type_str(p.PipeType),
				ptr04x(p.PipeHandle),
				p.MaximumTransferSize,
				p.PipeFlags);
		}
	}
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char* libdrv::select_configuration_str(char *buf, size_t len, const _URB_SELECT_CONFIGURATION *cfg)
{
	auto cd = cfg->ConfigurationDescriptor;
	if (!cd) {
		auto st = RtlStringCbPrintfA(buf, len, 
				"ConfigurationHandle %04x, ConfigurationDescriptor NULL (unconfigured)", 
				ptr04x(cfg->ConfigurationHandle));

		return st != STATUS_INVALID_PARAMETER ? buf : "select_configuration_str invalid parameter";
	}
	
	const char *result = buf;

	auto st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,
			"ConfigurationHandle %04x, "
			"ConfigurationDescriptor(bLength %d, bDescriptorType %d (must be %d), wTotalLength %hu, "
			"bNumInterfaces %d, bConfigurationValue %d, iConfiguration %d, bmAttributes %#x, MaxPower %d)",
		        ptr04x(cfg->ConfigurationHandle),
			cd->bLength,
			cd->bDescriptorType,
			USB_CONFIGURATION_DESCRIPTOR_TYPE,
			cd->wTotalLength,
			cd->bNumInterfaces,
			cd->bConfigurationValue,
			cd->iConfiguration,
			cd->bmAttributes,
			cd->MaxPower);

	if (!st) {
		auto cfg_end = get_configuration_end(cfg);
		interfaces_str(buf, len, &cfg->Interface, cd->bNumInterfaces, cfg_end);
	}

	return result && *result ? result : "select_configuration_str error";
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char* libdrv::select_interface_str(char *buf, size_t len, const _URB_SELECT_INTERFACE &iface)
{
	const char *result = buf;
	auto st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0, 
				       "ConfigurationHandle %04x", ptr04x(iface.ConfigurationHandle));

	if (!st) {
		interfaces_str(buf, len, &iface.Interface, 1, nullptr);
	}

	return result && *result ? result : "select_interface_str error";
}

/*
 * Use ExFreePoolWithTag to free the allocated memory.
 * Do not deallocate _URB_SELECT_CONFIGURATION.ConfigurationDescriptor
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_URB_SELECT_CONFIGURATION* libdrv::clone(
	_Out_ ULONG &size, _In_ const _URB_SELECT_CONFIGURATION &src, _In_ POOL_FLAGS flags, _In_ ULONG pooltag)
{
	auto cd_len = src.ConfigurationDescriptor ? src.ConfigurationDescriptor->wTotalLength : 0;
	size = src.Hdr.Length + cd_len;

	auto dst = (_URB_SELECT_CONFIGURATION*)ExAllocatePool2(flags | POOL_FLAG_UNINITIALIZED, size, pooltag);
	if (!dst) {
		return dst;
	}

	RtlCopyMemory(dst, &src, src.Hdr.Length);

	if (cd_len) {
		dst->ConfigurationDescriptor = 
			reinterpret_cast<USB_CONFIGURATION_DESCRIPTOR*>((char*)dst + src.Hdr.Length);

		RtlCopyMemory(dst->ConfigurationDescriptor, src.ConfigurationDescriptor, cd_len);
	}

	return dst;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_URB_SELECT_INTERFACE* libdrv::clone(
	_In_ const _URB_SELECT_INTERFACE &r, _In_ POOL_FLAGS Flags, _In_ ULONG PoolTag)
{
	auto &len = r.Hdr.Length;

	auto ptr = (_URB_SELECT_INTERFACE*)ExAllocatePool2(Flags | POOL_FLAG_UNINITIALIZED, len, PoolTag);
	if (ptr) {
		RtlCopyMemory(ptr, &r, len);
	}

	return ptr;
}
