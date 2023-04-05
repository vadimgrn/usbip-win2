/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "filter_request.h"
#include "trace.h"
#include "filter_request.tmh"

#include <ude_filter/request.h>

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::filter::unpack_request(_Inout_ _URB_CONTROL_TRANSFER_EX &r)
{
        auto func = get_function(r, true);

        if (auto &pkt = get_setup_packet(r); func == URB_FUNCTION_SELECT_CONFIGURATION) {
                auto urb = reinterpret_cast<_URB_SELECT_CONFIGURATION*>(r.TransferBuffer);
                auto cd = urb->ConfigurationDescriptor; // null if unconfigured

                pkt.bmRequestType.s.Recipient = USB_RECIP_DEVICE;
                pkt.bRequest = USB_REQUEST_SET_CONFIGURATION;
                pkt.wValue.W = cd ? cd->bConfigurationValue : 0; // FIXME: can't pass -1 if unconfigured
                pkt.wIndex.W = 0;
        } else if (func == URB_FUNCTION_SELECT_INTERFACE) {
                auto urb = reinterpret_cast<_URB_SELECT_INTERFACE*>(r.TransferBuffer);
                auto &intf = urb->Interface;

                pkt.bmRequestType.s.Recipient = USB_RECIP_INTERFACE;
                pkt.bRequest = USB_REQUEST_SET_INTERFACE;
                pkt.wValue.W = intf.AlternateSetting;
                pkt.wIndex.W = intf.InterfaceNumber;
        }

        NT_ASSERT(!r.TransferBufferLength);
}
