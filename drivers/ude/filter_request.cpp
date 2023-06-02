/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "filter_request.h"
#include "trace.h"
#include "filter_request.tmh"

#include "endpoint_list.h"
#include "device_ioctl.h"

#include <ude_filter/request.h>

#include <libdrv/dbgcommon.h>
#include <libdrv/usbdsc.h>
#include <libdrv/select.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void update_pipe_properties(_In_ device_ctx &dev, _In_ const USBD_INTERFACE_INFORMATION &intf)
{
        for (ULONG i = 0; i < intf.NumberOfPipes; ++i) {

                if (auto &p = intf.Pipes[i]; auto endp = find_endpoint(dev, compare_endpoint_descr(p))) {
                        TraceDbg("interface %d.%d, Pipes[%lu] {%s, addr %#x} -> PipeHandle %04x (was %04x)",
                                intf.InterfaceNumber, intf.AlternateSetting, i, usbd_pipe_type_str(p.PipeType), 
                                p.EndpointAddress, ptr04x(p.PipeHandle), ptr04x(endp->PipeHandle));

                        endp->PipeHandle = p.PipeHandle;
                        // endp->interface_number = intf.InterfaceNumber;
                        // endp->alternate_setting = intf.AlternateSetting;
                } else {
                        Trace(TRACE_LEVEL_ERROR, "interface %d.%d, Pipes[%lu] {%s, addr %#x} -> not found",
                                intf.InterfaceNumber, intf.AlternateSetting, i, usbd_pipe_type_str(p.PipeType),
                                p.EndpointAddress);
                }
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto clear_endpoint_stall(
        _In_ device_ctx &dev, _Inout_ USB_DEFAULT_PIPE_SETUP_PACKET &pkt, _Inout_ _URB_PIPE_REQUEST &r)
{
        if (auto endp = find_endpoint(dev, compare_endpoint_handle(r.PipeHandle))) {
                auto addr = endp->descriptor.bEndpointAddress;
                pkt = device::make_clear_endpoint_stall(addr);
                TraceDbg("PipeHandle %04x, bEndpointAddress %#x", ptr04x(r.PipeHandle), addr);
                return STATUS_SUCCESS;
        }

        Trace(TRACE_LEVEL_ERROR, "PipeHandle %04x not found", ptr04x(r.PipeHandle));

        r.Hdr.Status = USBD_STATUS_INVALID_PIPE_HANDLE;
        return STATUS_INVALID_HANDLE;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto select_configuration(
        _In_ device_ctx &dev, _Inout_ USB_DEFAULT_PIPE_SETUP_PACKET &pkt, _In_ const _URB_SELECT_CONFIGURATION &r)
{
        {
                char buf[libdrv::SELECT_CONFIGURATION_STR_BUFSZ];
                TraceDbg("%s", libdrv::select_configuration_str(buf, sizeof(buf), &r));
        }

        UCHAR cfg{}; // FIXME: can't pass -1 if unconfigured

        if (auto cd = r.ConfigurationDescriptor) { // null if unconfigured
                cfg = cd->bConfigurationValue;

                auto intf = &r.Interface;
                for (int i = 0; i < cd->bNumInterfaces; ++i, intf = usbdlib::next(intf)) {
                        update_pipe_properties(dev, *intf);
                }
        }

        pkt = device::make_set_configuration(cfg);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto select_interface(
        _In_ device_ctx &dev, _Inout_ USB_DEFAULT_PIPE_SETUP_PACKET &pkt, _In_ const _URB_SELECT_INTERFACE &r)
{
        {
                char buf[libdrv::SELECT_INTERFACE_STR_BUFSZ];
                TraceDbg("%s", libdrv::select_interface_str(buf, sizeof(buf), r));
        }

        auto &i = r.Interface;
        update_pipe_properties(dev, i);
        pkt = device::make_set_interface(i.InterfaceNumber, i.AlternateSetting);

        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::filter::unpack_request(
        _In_ device_ctx &dev, _Inout_ _URB_CONTROL_TRANSFER_EX &r, _In_ int function)
{
        NT_ASSERT(!r.TransferBufferLength);

        auto func_name = urb_function_str(function);
        TraceDbg("%s", func_name);

        NTSTATUS st;

        switch (auto &pkt = get_setup_packet(r); function) {
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
        case URB_FUNCTION_SYNC_RESET_PIPE:
        case URB_FUNCTION_SYNC_CLEAR_STALL:
                static_assert(is_request_function(URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL));
                static_assert(is_request_function(URB_FUNCTION_SYNC_RESET_PIPE));
                static_assert(is_request_function(URB_FUNCTION_SYNC_CLEAR_STALL));
                st = clear_endpoint_stall(dev, pkt, *reinterpret_cast<_URB_PIPE_REQUEST*>(r.TransferBuffer));
                break;
        case URB_FUNCTION_SELECT_INTERFACE:
                static_assert(is_request_function(URB_FUNCTION_SELECT_INTERFACE));
                st = select_interface(dev, pkt, *reinterpret_cast<_URB_SELECT_INTERFACE*>(r.TransferBuffer));
                break;
        case URB_FUNCTION_SELECT_CONFIGURATION:
                static_assert(is_request_function(URB_FUNCTION_SELECT_CONFIGURATION));
                st = select_configuration(dev, pkt, *reinterpret_cast<_URB_SELECT_CONFIGURATION*>(r.TransferBuffer));
                break;
        default:
                st = STATUS_INVALID_PARAMETER;
                r.Hdr.Status = USBD_STATUS_INVALID_PARAMETER;
                Trace(TRACE_LEVEL_ERROR, "Unexpected %s", func_name);
                NT_ASSERT(!"Unexpected URB Function");

        }

        return st;
}
