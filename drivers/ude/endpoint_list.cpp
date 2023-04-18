/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "endpoint_list.h"
#include "trace.h"
#include "endpoint_list.tmh"

#include <libdrv\lock.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto get_endpoint_list_head(_In_ device_ctx &dev)
{
        auto ep0 = get_endpoint_ctx(dev.ep0);
        return &ep0->entry;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::insert_endpoint_list(_In_ endpoint_ctx &endp)
{
        NT_ASSERT(IsListEmpty(&endp.entry));

        if (auto &dev = *get_device_ctx(endp.device); auto head = get_endpoint_list_head(dev)) {
                Lock lck(dev.endpoint_list_lock);
                InsertHeadList(head, &endp.entry); // outdated, but still not removed endpoints will be at end
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::remove_endpoint_list(_In_ endpoint_ctx &endp)
{
        auto e = &endp.entry;

        if (auto dev = get_device_ctx(endp.device)) {
                Lock lck(dev->endpoint_list_lock);
                RemoveEntryList(e); // works if entry was just InitializeListHead-ed
        }

        InitializeListHead(e);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto usbip::find_endpoint(_In_ device_ctx &dev, _In_ const compare_endpoint &compare) -> endpoint_ctx*
{
        auto head = get_endpoint_list_head(dev);

        Lock lck(dev.endpoint_list_lock);

        for (auto entry = head->Flink; entry != head; entry = entry->Flink) {
                auto endp = CONTAINING_RECORD(entry, endpoint_ctx, entry);
                if (compare(*endp)) {
                        return endp;
                }
        }

        return nullptr;
}

bool usbip::compare_endpoint_descr::operator() (const endpoint_ctx &endp) const
{
        auto &d = endp.descriptor;

        return  d.bEndpointAddress == pipe.EndpointAddress &&
                d.wMaxPacketSize == pipe.MaximumPacketSize &&
                d.bInterval == pipe.Interval &&
                usb_endpoint_type(d) == pipe.PipeType;
}
