/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "ioctl.h"
#include "trace.h"
#include "ioctl.tmh"

#include <usbip\vhci.h>

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_imported_devices(_In_ WDFREQUEST Request)
{
        ioctl_usbip_vhci_imported_dev *dev{};
        size_t buf_sz = 0;

        if (auto err = WdfRequestRetrieveOutputBuffer(Request, sizeof(*dev), &static_cast<void*>(dev), &buf_sz)) {
                return err;
        }

        auto cnt = buf_sz/sizeof(*dev);
        TraceMsg("Count %Iu", cnt);

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS plugin_hardware(_In_ WDFREQUEST Request)
{
        ioctl_usbip_vhci_plugin *r{};

        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &static_cast<void*>(r), nullptr)) {
                return err;
        }

        TraceMsg("%s:%s, busid %s, serial %s", r->host, r->service, r->busid, *r->serial ? r->serial : " ");
        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS unplug_hardware(_In_ WDFREQUEST Request)
{
        ioctl_usbip_vhci_unplug *r{};

        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &static_cast<void*>(r), nullptr)) {
                return err;
        }

        TraceMsg("Port #%d", r->port);
        return STATUS_NOT_IMPLEMENTED;
}
