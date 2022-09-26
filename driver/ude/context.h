/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

#include <initguid.h>
#include <usbip\vhci.h>

/*
 * Macro WDF_TYPE_NAME_TO_TYPE_INFO (see WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE)
 * makes impossible to declare context type with the same name in different namespaces.
 */

namespace usbip
{

struct vhci_context
{
        WDFQUEUE queue;

        UDECXUSBDEVICE devices[vhci::TOTAL_PORTS]; // do not access directly, functions must be used
        WDFSPINLOCK devices_lock;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(vhci_context, get_vhci_context)

struct usbdevice_context
{
        // from vhci::ioctl_plugin
        PSTR busid;
        UNICODE_STRING node_name;
        UNICODE_STRING service_name;
        UNICODE_STRING serial; // user-defined
        //

        WDFDEVICE vhci;
        bool destroyed;
        int port; // [1..vhci::TOTAL_PORTS], unique device id, this is not roothub's port number
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(usbdevice_context, get_usbdevice_context)

struct request_context
{
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(request_context, get_request_context)

} // namespace usbip
