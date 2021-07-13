#pragma once

#include "vhci_dev.h"

#include <usbspec.h>
#include <UdeCxTypes.h>
#include <UdeCxFuncEnum.h>
#include <UdeCxUsbDevice.h>

NTSTATUS
add_ep(pctx_vusb_t vusb, PUDECXUSBENDPOINT_INIT *pepinit, PUSB_ENDPOINT_DESCRIPTOR dscr_ep);

VOID
setup_ep_callbacks(PUDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS pcallbacks);

