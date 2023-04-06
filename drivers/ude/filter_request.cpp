/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "filter_request.h"
#include "trace.h"
#include "filter_request.tmh"

#include "context.h"
#include "device_ioctl.h"

#include <ude_filter/request.h>

#include <libdrv/dbgcommon.h>
#include <libdrv/select.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto clear_endpoint_stall(
        _In_ device_ctx &dev, _Inout_ USB_DEFAULT_PIPE_SETUP_PACKET &pkt, _In_ const _URB_PIPE_REQUEST &r)
{
        if (auto endp = find_endpoint(dev, r.PipeHandle)) {
                auto addr = endp->descriptor.bEndpointAddress;
                pkt = device::make_clear_endpoint_stall(addr);
                TraceDbg("PipeHandle %04x, bEndpointAddress %#x", ptr04x(r.PipeHandle), addr);
                return STATUS_SUCCESS;
        }

        Trace(TRACE_LEVEL_ERROR, "PipeHandle %04x not found", ptr04x(r.PipeHandle));
        return STATUS_INVALID_PARAMETER;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void select_configuration(_Inout_ USB_DEFAULT_PIPE_SETUP_PACKET &pkt, _In_ const _URB_SELECT_CONFIGURATION &r)
{
        {
                char buf[libdrv::SELECT_CONFIGURATION_STR_BUFSZ];
                TraceDbg("%s", libdrv::select_configuration_str(buf, sizeof(buf), &r));
        }

        auto cd = r.ConfigurationDescriptor; // null if unconfigured

        pkt.bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt.bRequest = USB_REQUEST_SET_CONFIGURATION;
        pkt.wValue.W = cd ? cd->bConfigurationValue : 0; // FIXME: can't pass -1 if unconfigured
        pkt.wIndex.W = 0;
        NT_ASSERT(!pkt.wLength);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void select_interface(_Inout_ USB_DEFAULT_PIPE_SETUP_PACKET &pkt, _In_ const _URB_SELECT_INTERFACE &r)
{
        {
                char buf[libdrv::SELECT_INTERFACE_STR_BUFSZ];
                TraceDbg("%s", libdrv::select_interface_str(buf, sizeof(buf), r));
        }
        
        auto &intf = r.Interface;

        pkt.bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
        pkt.bRequest = USB_REQUEST_SET_INTERFACE;
        pkt.wValue.W = intf.AlternateSetting;
        pkt.wIndex.W = intf.InterfaceNumber;
        NT_ASSERT(!pkt.wLength);
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

        auto st = STATUS_SUCCESS;

        switch (auto &pkt = get_setup_packet(r); function) {
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
        case URB_FUNCTION_SYNC_RESET_PIPE:
        case URB_FUNCTION_SYNC_CLEAR_STALL:
                st = clear_endpoint_stall(dev, pkt, *reinterpret_cast<_URB_PIPE_REQUEST*>(r.TransferBuffer));
                break;
        case URB_FUNCTION_SELECT_INTERFACE:
                select_interface(pkt, *reinterpret_cast<_URB_SELECT_INTERFACE*>(r.TransferBuffer));
                break;
        case URB_FUNCTION_SELECT_CONFIGURATION:
                select_configuration(pkt, *reinterpret_cast<_URB_SELECT_CONFIGURATION*>(r.TransferBuffer));
                break;
        default:
                Trace(TRACE_LEVEL_ERROR, "Unexpected %s", func_name);
                st = STATUS_INVALID_PARAMETER;
        }

        return st;
}
