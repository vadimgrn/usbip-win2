/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/ioctl.h>
#include <usbip/vhci.h>

namespace usbip
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto has_urb(_In_ WDFREQUEST request)
{
	auto irp = WdfRequestWdmGetIrp(request);
	return libdrv::has_urb(irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto try_get_urb(_In_ WDFREQUEST request)
{
	auto irp = WdfRequestWdmGetIrp(request);
	return libdrv::has_urb(irp) ? libdrv::urb_from_irp(irp) : nullptr;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_urb(_In_ WDFREQUEST request)
{
        auto irp = WdfRequestWdmGetIrp(request);
        NT_ASSERT(libdrv::has_urb(irp));
        return *libdrv::urb_from_irp(irp);
}

} // namespace usbip


namespace usbip::vhci::ioctl
{

/*
 * Kernel-only extension for self-sent reattach requests (target_self, KernelMode).
 * Preserves the original attaching caller's SIDs across reconnects when the
 * IOCTL arrives in KernelMode with no user-mode token (needed for session
 * isolation and per-user quota/DACL tracking, where config.iso.owner.sid is empty).
 */
struct plugin_hardware_ex : plugin_hardware
{
        sid_blob caller_user_sid;
        sid_blob caller_logon_sid;
};

/*
 * Kernel-only extension for self-sent async detach requests (target_self, KernelMode).
 * When has_owner is true (e.g. session logoff cleanup), scopes detach to devices
 * matching owner; when false, detaches with administrative scope.
 */
struct plugout_hardware_ex : plugout_hardware
{
        device_owner owner;
        bool has_owner;
};

} // namespace usbip::vhci::ioctl

