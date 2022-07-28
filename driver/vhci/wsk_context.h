/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "usbip_proto.h"
#include "mdl_cpp.h"

struct _IO_WORKITEM;
struct vpdo_dev_t;

inline LOOKASIDE_LIST_EX wsk_context_list;

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS init_wsk_context_list();

struct wsk_context
{
        vpdo_dev_t *vpdo;
        IRP *wsk_irp;

        IRP *irp; // can be NULL, see send_cmd_unlink
        _IO_WORKITEM *workitem;

        usbip_header hdr;

        usbip::Mdl mdl_hdr;
        usbip::Mdl mdl_buf; // describes URB_FROM_IRP(irp)->TransferBuffer
        usbip::Mdl mdl_isoc;

        usbip_iso_packet_descriptor *isoc;
        ULONG isoc_alloc_cnt;
        bool is_isoc;
};

_IRQL_requires_max_(DISPATCH_LEVEL)
wsk_context *alloc_wsk_context(_In_ ULONG NumberOfPackets);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS prepare_isoc(_In_ wsk_context &ctx, _In_ ULONG NumberOfPackets);

_IRQL_requires_max_(DISPATCH_LEVEL)
void reuse(_In_ wsk_context &ctx);

_IRQL_requires_max_(DISPATCH_LEVEL)
void free(_In_opt_ wsk_context *ctx, _In_ bool reuse = true);

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto number_of_packets(_In_ const wsk_context &ctx)
{
        return ctx.mdl_isoc.size()/sizeof(*ctx.isoc);
}
