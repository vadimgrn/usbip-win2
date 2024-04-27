/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

#include <usbip\proto.h>
#include <libdrv\mdl_cpp.h>

namespace usbip
{

struct device_ctx;

struct wsk_context
{
        device_ctx *dev; // UDECXUSBDEVICE can be obtained from WDFREQUEST, but it is optional

        // transient data

        WDFREQUEST request; // can be WDF_NO_HANDLE
        Mdl mdl_buf; // describes URB_FROM_IRP()->TransferBuffer(MDL)

        // preallocated data

        IRP *wsk_irp;

        Mdl mdl_hdr;
        usbip_header hdr;

        Mdl mdl_isoc;
        usbip_iso_packet_descriptor *isoc;
        ULONG isoc_alloc_cnt;
        bool is_isoc;
};


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS init_wsk_context_list(_In_ ULONG tag);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void delete_wsk_context_list();


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
wsk_context *alloc_wsk_context(_In_ device_ctx *dev, _In_opt_ WDFREQUEST request, _In_ ULONG NumberOfPackets = 0);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void free(_In_opt_ wsk_context *ctx, _In_ bool reuse_irp);


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS prepare_isoc(_In_ wsk_context &ctx, _In_ ULONG NumberOfPackets);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto number_of_packets(_In_ const wsk_context &ctx)
{
        return ctx.mdl_isoc.size()/sizeof(*ctx.isoc);
}

class wsk_context_ptr 
{
public:
        template<typename... Args>
        wsk_context_ptr(Args&&... args) : m_ctx(alloc_wsk_context(args...)) {}

        wsk_context_ptr(wsk_context *ptr, bool reuse) : m_reuse(reuse), m_ctx(ptr) {}
        ~wsk_context_ptr() { free(m_ctx, m_reuse); }

        wsk_context_ptr(const wsk_context_ptr&) = delete;
        wsk_context_ptr& operator =(const wsk_context_ptr&) = delete;

        wsk_context_ptr(wsk_context_ptr&& ctx) : m_reuse(ctx.m_reuse), m_ctx(ctx.release()) {}
        wsk_context_ptr& operator =(wsk_context_ptr&& ctx);

        explicit operator bool() const { return m_ctx; }
        auto operator !() const { return !m_ctx; }

        auto operator ->() const { return m_ctx; }
        auto& operator *() const { return *m_ctx; }

        seqnum_t seqnum(bool byte_swap) const;

        wsk_context *release();
        void reset(wsk_context *ctx, bool reuse);

private:
        bool m_reuse{};
        wsk_context *m_ctx{};
};

} // namespace usbip
