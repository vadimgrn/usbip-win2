/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "usbip_proto.h"
#include "mdl_cpp.h"

struct vpdo_dev_t;

extern LOOKASIDE_LIST_EX send_context_list;
NTSTATUS init_send_context_list();

struct send_context
{
        vpdo_dev_t *vpdo;
        IRP *irp;
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

send_context *alloc_send_context(_In_ ULONG NumberOfPackets = 0);
void free(_In_ send_context *ctx, _In_ bool reuse = true);

inline auto number_of_packets(_In_ const send_context &ctx)
{
        return ctx.mdl_isoc.size()/sizeof(*ctx.isoc);
}
