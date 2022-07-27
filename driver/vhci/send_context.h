/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "usbip_proto.h"
#include "mdl_cpp.h"

struct vpdo_dev_t;

inline LOOKASIDE_LIST_EX send_context_list;

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS init_send_context_list();

struct send_context
{
        vpdo_dev_t *vpdo;
        IRP *irp; // can be NULL, see send_cmd_unlink
        IRP *wsk_irp;

        usbip_header hdr;

        usbip::Mdl mdl_hdr;
        usbip::Mdl mdl_buf;
        usbip::Mdl mdl_isoc;

        usbip_iso_packet_descriptor *isoc;
        ULONG isoc_alloc_cnt;
        bool is_isoc;
};

static_assert(sizeof(usbip_header) == 48);
static_assert(sizeof(usbip::Mdl) == 16);
static_assert(sizeof(usbip_iso_packet_descriptor) == 16);

_IRQL_requires_max_(DISPATCH_LEVEL)
send_context *alloc_send_context(_In_ ULONG NumberOfPackets);

_IRQL_requires_max_(DISPATCH_LEVEL)
void reuse(_In_opt_ send_context *ctx);

_IRQL_requires_max_(DISPATCH_LEVEL)
void free(_In_opt_ send_context *ctx, _In_ bool reuse = true);

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto number_of_packets(_In_ const send_context &ctx)
{
        return ctx.mdl_isoc.size()/sizeof(*ctx.isoc);
}
