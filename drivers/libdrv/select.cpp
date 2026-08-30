/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "select.h"
#include "codeseg.h"
#include "dbgcommon.h"

#include <stddef.h>
#include <ntintsafe.h>
#include <ntstrsafe.h>

namespace
{

using namespace usbip;
using libdrv::is_valid;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
USBD_INTERFACE_INFORMATION *next_interface(_In_ const USBD_INTERFACE_INFORMATION *iface, _In_opt_ const void *cfg_end)
{
        if (!is_valid(iface, cfg_end)) {
                return nullptr;
        }

        void *next = (char*)iface + iface->Length;

        if (cfg_end && next >= cfg_end) {
                return nullptr;
        }

        return static_cast<USBD_INTERFACE_INFORMATION*>(next);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline const void *get_configuration_end(_In_ const _URB_SELECT_CONFIGURATION *cfg)
{
	return (char*)cfg + cfg->Hdr.Length;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void interfaces_str(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ const USBD_INTERFACE_INFORMATION *r, _In_ int cnt, 
	_In_opt_ const void *cfg_end)
{
	auto st = STATUS_SUCCESS;

	for (int i = 0; i < cnt && !st && is_valid(r, cfg_end);
	     ++i, r = next_interface(r, cfg_end)) {

		st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0,
			"\nInterface(Length %d, InterfaceNumber %d, AlternateSetting %d, "
			"Class %#x, SubClass %#x, Protocol %#x, InterfaceHandle %04x, NumberOfPipes %lu)", 
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
				"\nPipes[%lu](MaximumPacketSize %d, EndpointAddress %#x{%s[%d]}, Interval %d, %s, "
				"PipeHandle %04x, MaximumTransferSize %lu, PipeFlags %#lx)",
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
const char* libdrv::select_configuration_str(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ const _URB_SELECT_CONFIGURATION *cfg)
{
	if (!(buf && len && cfg &&
	      cfg->Hdr.Length >= offsetof(_URB_SELECT_CONFIGURATION, Interface))) {
		return "select_configuration_str invalid parameter";
	}

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
			"ConfigurationDescriptor(bLength %d, bDescriptorType %d (must be %d), wTotalLength %d, "
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
const char* libdrv::select_interface_str(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ const _URB_SELECT_INTERFACE &iface)
{
	if (!(buf && len)) {
		return "select_interface_str invalid parameter";
	}

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
        _Out_ ULONG &size, _In_ const _URB_SELECT_CONFIGURATION &src, _In_ POOL_TYPE pool_type, _In_ ULONG pooltag)
{
        size = 0;

        if (src.Hdr.Length < offsetof(_URB_SELECT_CONFIGURATION, Interface)) {
                return nullptr;
        }

        if (KeGetCurrentIrql() == DISPATCH_LEVEL &&
            (pool_type == PagedPool || pool_type == PagedPoolCacheAligned)) [[unlikely]] {
                NT_ASSERT(!"cannot allocate from paged pool at high IRQL"); 
                return nullptr;
        }

        auto cd = src.ConfigurationDescriptor;
        ULONG cd_len = cd ? cd->wTotalLength : 0;

        if (cd && cd_len < sizeof(*cd)) {
                return nullptr;
        }

        auto aligned_hdr_len = ALIGN_UP_BY(src.Hdr.Length, alignof(_URB_SELECT_CONFIGURATION));
        auto local_size = aligned_hdr_len + cd_len;

        auto dst = (_URB_SELECT_CONFIGURATION*)ExAllocatePoolUninitialized(pool_type, local_size, pooltag);
        if (!dst) {
                return nullptr;
        }

        RtlCopyMemory(dst, &src, src.Hdr.Length);

        if (aligned_hdr_len > src.Hdr.Length) { // zero alignment gap
                RtlZeroMemory(reinterpret_cast<char*>(dst) + src.Hdr.Length, aligned_hdr_len - src.Hdr.Length);
        }

        if (cd && cd_len) {
                dst->ConfigurationDescriptor =
                        reinterpret_cast<USB_CONFIGURATION_DESCRIPTOR*>((char*)dst + aligned_hdr_len);

                RtlCopyMemory(dst->ConfigurationDescriptor, cd, cd_len);
        } else {
                dst->ConfigurationDescriptor = nullptr;
        }

        size = static_cast<ULONG>(local_size);
        return dst;
}

/*
 * Why `iface->Length < != min_len` would cause issues
 * - Interfaces with `NumberOfPipes == 0` would be rejected
 * - Windows USB core driver validation (`usbport.sys`) only checks `>=`
 * - The OS requires that `Length` is *at least* the minimum needed to hold
 *   the declared pipes (`Length >= Length`), not strictly equal.
 * - For `URB_FUNCTION_SELECT_INTERFACE`, `cfg_end` is `nullptr` and there is
 *   only a single interface. Requiring exact equality would break valid client
 *   driver requests that allocated standard buffer sizes where `Length > min_len`.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::is_valid(_In_ const _USBD_INTERFACE_INFORMATION *iface, _In_opt_ const void *cfg_end)
{
        if (!iface) {
                return false;
        }

        const auto pipes_offset = offsetof(USBD_INTERFACE_INFORMATION, Pipes);

        const auto begin = reinterpret_cast<uintptr_t>(iface);
        const auto end = reinterpret_cast<uintptr_t>(cfg_end);

        if (end && !(end >= begin && (end - begin) >= pipes_offset)) {
                return false; // not enough room to safely read the fixed header
        }

        ULONG pipes_len{};
        if (NT_ERROR(RtlULongMult(iface->NumberOfPipes, sizeof(*iface->Pipes), &pipes_len))) {
                return false;
        }

        ULONG min_len;
        if (NT_ERROR(RtlULongAdd(pipes_offset, pipes_len, &min_len))) {
                return false;
        } else if (iface->Length < min_len) { // != is wrong here, see comments
                return false;
        }

        return !end || (end - begin) >= iface->Length;
}
