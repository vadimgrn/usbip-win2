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

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::filter::unpack_request(
        _In_ const device_ctx &dev, _Inout_ _URB_CONTROL_TRANSFER_EX &ctrl, _In_ USHORT function)
{
        NT_ASSERT(!ctrl.TransferBufferLength);
        char buf[max(libdrv::SELECT_CONFIGURATION_STR_BUFSZ, libdrv::SELECT_INTERFACE_STR_BUFSZ)];

        switch (auto &pkt = get_setup_packet(ctrl); function) {
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
        case URB_FUNCTION_SYNC_RESET_PIPE:
        case URB_FUNCTION_SYNC_CLEAR_STALL:
                if (auto &r = *reinterpret_cast<_URB_PIPE_REQUEST*>(ctrl.TransferBuffer);
                    auto endp = find_endpoint(dev, r.PipeHandle)) {
                        TraceDbg("%s, PipeHandle %04x", urb_function_str(function), ptr04x(r.PipeHandle));
                        pkt = device::make_clear_endpoint_stall(endp->descriptor.bEndpointAddress);
                } else {
                        return STATUS_INVALID_PARAMETER;
                }
                break;
        case URB_FUNCTION_SELECT_INTERFACE:
                if (auto r = reinterpret_cast<_URB_SELECT_INTERFACE*>(ctrl.TransferBuffer)) {
                        TraceDbg("%s", libdrv::select_interface_str(buf, sizeof(buf), *r));
                        auto &intf = r->Interface;

                        pkt.bmRequestType.s.Recipient = USB_RECIP_INTERFACE;
                        pkt.bRequest = USB_REQUEST_SET_INTERFACE;
                        pkt.wValue.W = intf.AlternateSetting;
                        pkt.wIndex.W = intf.InterfaceNumber;
                }
                break;
        case URB_FUNCTION_SELECT_CONFIGURATION:
                if (auto r = reinterpret_cast<_URB_SELECT_CONFIGURATION*>(ctrl.TransferBuffer)) {
                        TraceDbg("%s", libdrv::select_configuration_str(buf, sizeof(buf), r));
                        auto cd = r->ConfigurationDescriptor; // null if unconfigured

                        pkt.bmRequestType.s.Recipient = USB_RECIP_DEVICE;
                        pkt.bRequest = USB_REQUEST_SET_CONFIGURATION;
                        pkt.wValue.W = cd ? cd->bConfigurationValue : 0; // FIXME: can't pass -1 if unconfigured
                        pkt.wIndex.W = 0;
                }
                break;
        default:
                Trace(TRACE_LEVEL_ERROR, "Unexpected %s", urb_function_str(function));
                return STATUS_INVALID_PARAMETER;
        }

        return STATUS_SUCCESS;
}
