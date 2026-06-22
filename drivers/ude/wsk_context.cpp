/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_context.h"
#include "trace.h"
#include "wsk_context.tmh"

#include "driver.h"

namespace
{

using namespace usbip;

bool g_initialized;
LOOKASIDE_LIST_EX g_lookaside;

_IRQL_requires_same_
_Function_class_(free_function_ex)
void free_function_ex(_In_ __drv_freesMem(Mem) void *Buffer, _Inout_ LOOKASIDE_LIST_EX*)
{
        auto ctx = static_cast<wsk_context*>(Buffer);
        NT_ASSERT(ctx);

        TraceWSK("%04x, isoc[%Iu]", ptr04x(ctx), ctx->isoc_alloc_cnt);

        ctx->mdl_hdr.reset();
        ctx->mdl_buf.reset();
        ctx->mdl_buf_tail.reset();
        ctx->mdl_isoc.reset();
        ctx->wsk_irp.reset();

        for (void* v[] { ctx->buf_tail, ctx->isoc, ctx }; auto ptr: v) {
                unique_ptr{ptr};
        }
}

_IRQL_requires_same_
_Function_class_(allocate_function_ex)
void *allocate_function_ex(
        _In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes, _In_ [[maybe_unused]] ULONG Tag, _Inout_ LOOKASIDE_LIST_EX *list)
{
        NT_ASSERT(PoolType == NonPagedPoolNx);
        wsk_context *ctx{};

        if (unique_ptr ptr(PoolType, NumberOfBytes); !ptr) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", NumberOfBytes);
                return nullptr;
        } else {
                ctx = ptr.release<wsk_context>();
                NT_ASSERT(Tag == ptr.pooltag);
        }

        ctx->mdl_hdr = Mdl(&ctx->hdr, sizeof(ctx->hdr));

        if (auto err = ctx->mdl_hdr.prepare_nonpaged()) {
                Trace(TRACE_LEVEL_ERROR, "mdl_hdr %!STATUS!", err);
                free_function_ex(ctx, list);
                return nullptr;
        }

        if (ctx->wsk_irp = libdrv::irp_ptr(1, false); !ctx->wsk_irp) {
                Trace(TRACE_LEVEL_ERROR, "IoAllocateIrp -> NULL");
                free_function_ex(ctx, list);
                return nullptr;
        }

        TraceWSK("%04x", ptr04x(ctx));
        return ctx;
}

/*
 * If use ExFreeToLookasideListEx in case of error, next ExAllocateFromLookasideListEx will return the same pointer.
 * free_function_ex is used instead in hope that next object in the LookasideList may have required buffer.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto alloc_wsk_context(_In_ ULONG NumberOfPackets)
{
        auto ctx = (wsk_context*)ExAllocateFromLookasideListEx(&g_lookaside);

        if (!ctx) {
                Trace(TRACE_LEVEL_ERROR, "ExAllocateFromLookasideListEx error");
        } else if (auto err = prepare_isoc(*ctx, NumberOfPackets)) {
                Trace(TRACE_LEVEL_ERROR, "prepare_isoc(NumberOfPackets %lu) %!STATUS!", NumberOfPackets, err);
                free_function_ex(ctx, &g_lookaside);
                ctx = nullptr;
        }

        return ctx;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto alloc_buf_tail(_Inout_ void* &buf, _In_ ULONG length)
{
        enum { MAXLEN = PAGE_SIZE/2 }; // arbitrary
        auto st = STATUS_SUCCESS;

        if (length > MAXLEN) {
                Trace(TRACE_LEVEL_ERROR, "length %lu > MAXLEN %d", length, MAXLEN);
                st = STATUS_BUFFER_TOO_SMALL;
        } else if (buf) {
                // allocate once
        } else if (unique_ptr ptr(libdrv::uninitialized, NonPagedPoolNx, MAXLEN); !ptr) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate %d bytes", MAXLEN);
                st = STATUS_INSUFFICIENT_RESOURCES;
        } else {
                buf = ptr.release();
        }

        return st;
}

} // namespace


/*
 * LOOKASIDE_LIST_EX.L.Depth is zero if Driver Verifier is enabled.
 * For this reason ExFreeToLookasideListEx always calls L.FreeEx instead of InterlockedPushEntrySList.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::init_wsk_context_list()
{
        if (g_initialized) {
                return STATUS_ALREADY_INITIALIZED;
        }

        auto err = ExInitializeLookasideListEx(&g_lookaside, allocate_function_ex, free_function_ex, 
                                               NonPagedPoolNx, 0, sizeof(wsk_context), unique_ptr::pooltag, 0);

        g_initialized = !err;
        return err;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::delete_wsk_context_list()
{
        if (g_initialized) {
                ExDeleteLookasideListEx(&g_lookaside);
                g_initialized = false;
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto usbip::alloc_wsk_context(
        _In_ device_ctx *dev, _In_opt_ WDFREQUEST request, _In_ ULONG NumberOfPackets) -> wsk_context*
{
        NT_ASSERT(dev);

        auto ctx = ::alloc_wsk_context(NumberOfPackets);
        if (ctx) {
                ctx->dev = dev;
                ctx->request = request;
        }

        return ctx;
}

/*
 * alloc_wsk_context sets dev, request, is_isoc. It's safe do not clear them.
 * Retain mdl_buf_tail.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::free(_In_opt_ wsk_context *ctx, _In_ bool reuse_irp)
{
        if (!ctx) {
                return;
        }

        ctx->mdl_buf.reset();

        if (reuse_irp) {
                IoReuseIrp(ctx->wsk_irp.get(), STATUS_SUCCESS);
        }

        ExFreeToLookasideListEx(&g_lookaside, ctx);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::alloc_mdl_buf_tail(_Inout_ wsk_context &ctx, _In_ ULONG length)
{
        if (auto err = alloc_buf_tail(ctx.buf_tail, length)) {
                return err;
        }

        auto st = STATUS_SUCCESS;

        if (auto &mdl = ctx.mdl_buf_tail; mdl.size() == length) {
                NT_ASSERT(!mdl.next());
        } else if (mdl = Mdl(ctx.buf_tail, length); !mdl) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate MDL");
                st = STATUS_INSUFFICIENT_RESOURCES;
        } else {
                st = mdl.prepare_nonpaged();
        }

        return st;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::prepare_isoc(_Inout_ wsk_context &ctx, _In_ ULONG NumberOfPackets)
{
        NT_ASSERT(NumberOfPackets != number_of_packets_non_isoch);

        ctx.is_isoc = NumberOfPackets;
        if (!ctx.is_isoc) {
                return STATUS_SUCCESS;
        }

        ULONG isoc_len = NumberOfPackets*sizeof(*ctx.isoc);

        if (ctx.isoc_alloc_cnt < NumberOfPackets) {
                unique_ptr isoc(NonPagedPoolNx, isoc_len);
                if (!isoc) {
                        return STATUS_INSUFFICIENT_RESOURCES;
                }

                if (ctx.isoc) {
                        unique_ptr(ctx.isoc);
                }

                ctx.isoc = isoc.release<iso_packet_descriptor>();
                ctx.isoc_alloc_cnt = NumberOfPackets;

                ctx.mdl_isoc.reset();
        }

        if (ctx.mdl_isoc.size() != isoc_len) {
                ctx.mdl_isoc = Mdl(ctx.isoc, isoc_len);
                if (auto err = ctx.mdl_isoc.prepare_nonpaged()) {
                        return err;
                }
                NT_ASSERT(number_of_packets(ctx) == NumberOfPackets);
        }

        return STATUS_SUCCESS;
}

auto usbip::wsk_context_ptr::operator =(wsk_context_ptr&& ctx) -> wsk_context_ptr&
{
        auto reuse = ctx.m_reuse;
        reset(ctx.release(), reuse);
        return *this;
}

void usbip::wsk_context_ptr::reset(wsk_context *ctx, bool reuse)
{
        if (m_ctx != ctx) {
                free(m_ctx, m_reuse);
                m_reuse = reuse;
                m_ctx = ctx;
        }
}

wsk_context* usbip::wsk_context_ptr::release()
{ 
        auto tmp = m_ctx;
        m_ctx = nullptr; 
        return tmp;
}

seqnum_t usbip::wsk_context_ptr::seqnum(bool byte_swap) const
{
        NT_ASSERT(m_ctx);
        
        auto seqnum = m_ctx->hdr.seqnum;
        static_assert(sizeof(seqnum) == sizeof(unsigned long));

        return byte_swap ? RtlUlongByteSwap(seqnum) : seqnum;
}
