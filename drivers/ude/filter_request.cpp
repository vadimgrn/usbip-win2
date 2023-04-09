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
#include <libdrv/usbdsc.h>
#include <libdrv/select.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void update_pipe_handles(_In_ device_ctx &dev, _In_ const USBD_INTERFACE_INFORMATION &intf)
{
        for (ULONG i = 0; i < intf.NumberOfPipes; ++i) {

                if (auto &p = intf.Pipes[i]; auto endp = find_endpoint(dev, p)) {
                        TraceDbg("interface %d.%d, pipe[%lu] {%s, addr %#x} -> PipeHandle %04x (was %04x)",
                                intf.InterfaceNumber, intf.AlternateSetting, i, usbd_pipe_type_str(p.PipeType), 
                                p.EndpointAddress, ptr04x(p.PipeHandle), ptr04x(endp->PipeHandle));

                        endp->PipeHandle = p.PipeHandle;
                } else {
                        Trace(TRACE_LEVEL_ERROR, "interface %d.%d, pipe[%lu] {%s, addr %#x} -> not found",
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
        if (auto endp = find_endpoint(dev, r.PipeHandle)) {
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

        if (auto cd = r.ConfigurationDescriptor; cd) {
                auto intf = &r.Interface;
                for (int i = 0; i < cd->bNumInterfaces; ++i, intf = usbdlib::advance(intf)) {
                        update_pipe_handles(dev, *intf);
                }
        }

        if (dev.skip_select_config) {
                return STATUS_REQUEST_NOT_ACCEPTED;
        }

        auto cd = r.ConfigurationDescriptor; // null if unconfigured
        auto cfg = cd ? cd->bConfigurationValue : UCHAR(0); // FIXME: can't pass -1 if unconfigured

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
        
        update_pipe_handles(dev, r.Interface);

        auto &i = r.Interface;
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
                st = clear_endpoint_stall(dev, pkt, *reinterpret_cast<_URB_PIPE_REQUEST*>(r.TransferBuffer));
                break;
        case URB_FUNCTION_SELECT_INTERFACE:
                st = select_interface(dev, pkt, *reinterpret_cast<_URB_SELECT_INTERFACE*>(r.TransferBuffer));
                break;
        case URB_FUNCTION_SELECT_CONFIGURATION:
                st = select_configuration(dev, pkt, *reinterpret_cast<_URB_SELECT_CONFIGURATION*>(r.TransferBuffer));
                break;
        default:
                st = STATUS_UNSUCCESSFUL;
                r.Hdr.Status = USBD_STATUS_INVALID_URB_FUNCTION;
                Trace(TRACE_LEVEL_ERROR, "Unexpected %s", func_name);
        }

        return st;
}
