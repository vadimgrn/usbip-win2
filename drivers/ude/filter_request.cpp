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
constexpr CCHAR get_priority_boost(_In_ int cls, _In_ int subclass, _In_ int proto)
{
        switch (cls) {
        case USB_DEVICE_CLASS_STORAGE:
                return IO_DISK_INCREMENT;
        case USB_DEVICE_CLASS_AUDIO:
                return IO_SOUND_INCREMENT;
        case USB_DEVICE_CLASS_VIDEO:
                return IO_VIDEO_INCREMENT;
        case USB_DEVICE_CLASS_AUDIO_VIDEO:
                switch (subclass) {
                case 2: // AVData Video Streaming Interface
                        return IO_VIDEO_INCREMENT;
                case 3: // AVData Audio Streaming Interface
                        return IO_SOUND_INCREMENT;
                }
                break;
        case USB_DEVICE_CLASS_HUMAN_INTERFACE:
                static_assert(IO_MOUSE_INCREMENT == IO_KEYBOARD_INCREMENT);
                return IO_KEYBOARD_INCREMENT;
        case USB_DEVICE_CLASS_WIRELESS_CONTROLLER:
                return IO_NETWORK_INCREMENT;
        case USB_DEVICE_CLASS_COMMUNICATIONS:
        case USB_DEVICE_CLASS_PRINTER:
        case USB_DEVICE_CLASS_SMART_CARD:
                return IO_SERIAL_INCREMENT;
        case USB_DEVICE_CLASS_MISCELLANEOUS:
                switch (subclass) {
                case 4: // RNDIS over XXX
                        return IO_NETWORK_INCREMENT;
                case 5: // Machine Vision Device conforming to the USB3 Vision specification
                        switch (proto) {
                        case 2: // USB3 Vision Streaming Interface
                                return IO_VIDEO_INCREMENT;
                        }
                        break;
                }
                break;
        }

        return IO_NO_INCREMENT;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void update_pipe_properties(_In_ device_ctx &dev, _In_ const USBD_INTERFACE_INFORMATION &intf)
{
        for (ULONG i = 0; i < intf.NumberOfPipes; ++i) {
                auto &pipe = intf.Pipes[i];

                auto endp = find_endpoint(dev, pipe.EndpointAddress);
                if (!endp) {
                        Trace(TRACE_LEVEL_ERROR, 
                                "interface %d.%d, Pipes[%lu], EndpointAddress %#x{%s} -> not found",
                                intf.InterfaceNumber, intf.AlternateSetting, i, pipe.EndpointAddress, 
                                usbd_pipe_type_str(pipe.PipeType));

                        continue;
                }

                NT_ASSERT(usb_endpoint_type(endp->descriptor) == pipe.PipeType);

                if (pipe.PipeType == UsbdPipeTypeControl) {
                        //
                } else if (auto boost = get_priority_boost(intf.Class, intf.SubClass, intf.Protocol)) {
                        endp->priority_boost = boost;
                }

                TraceDbg("interface %d.%d, %#x/%#x/%#x, Pipes[%lu], EndpointAddress %#x{%s %s[%d]} -> "
                         "PipeHandle %04x (was %04x), PriorityBoost %d",
                        intf.InterfaceNumber, intf.AlternateSetting, intf.Class, intf.SubClass, intf.Protocol,
                        i, pipe.EndpointAddress, usbd_pipe_type_str(pipe.PipeType),
                        usb_endpoint_dir_out(endp->descriptor) ? "Out" : "In", usb_endpoint_num(endp->descriptor),
                        ptr04x(pipe.PipeHandle), ptr04x(endp->PipeHandle), endp->priority_boost);

                endp->PipeHandle = pipe.PipeHandle;
                // endp->interface_number = intf.InterfaceNumber;
                // endp->alternate_setting = intf.AlternateSetting;
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

        UCHAR cfg{}; // FIXME: can't pass -1 if unconfigured

        if (auto cd = r.ConfigurationDescriptor) { // null if unconfigured
                cfg = cd->bConfigurationValue;

                auto intf = &r.Interface;
                for (int i = 0; i < cd->bNumInterfaces; ++i, intf = libdrv::next(intf)) {
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
