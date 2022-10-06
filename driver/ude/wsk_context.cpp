/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_context.h"
#include "trace.h"
#include "wsk_context.tmh"

namespace
{

using namespace usbip;

ULONG g_tag;
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
        ctx->mdl_isoc.reset();

        if (auto irp = ctx->wsk_irp) {
                IoFreeIrp(irp);
        }

        if (auto ptr = ctx->isoc) {
                ExFreePoolWithTag(ptr, g_tag);
        }

        ExFreePoolWithTag(ctx, g_tag);
}

_IRQL_requires_same_
_Function_class_(allocate_function_ex)
void *allocate_function_ex(_In_ [[maybe_unused]] POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes, _In_ ULONG Tag, _Inout_ LOOKASIDE_LIST_EX *list)
{
        NT_ASSERT(PoolType == NonPagedPoolNx);
        NT_ASSERT(Tag == g_tag);

        auto ctx = (wsk_context*)ExAllocatePool2(POOL_FLAG_NON_PAGED, NumberOfBytes, Tag);
        if (!ctx) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", NumberOfBytes);
                return nullptr;
        }

        ctx->mdl_hdr = Mdl(&ctx->hdr, sizeof(ctx->hdr));

        if (auto err = ctx->mdl_hdr.prepare_nonpaged()) {
                Trace(TRACE_LEVEL_ERROR, "mdl_hdr %!STATUS!", err);
                free_function_ex(ctx, list);
                return nullptr;
        }

        ctx->wsk_irp = IoAllocateIrp(1, false);
        if (!ctx->wsk_irp) {
                Trace(TRACE_LEVEL_ERROR, "IoAllocateIrp -> NULL");
                free_function_ex(ctx, list);
                return nullptr;
        }

        TraceWSK("-> %04x", ptr04x(ctx));
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

} // namespace


/*
 * LOOKASIDE_LIST_EX.L.Depth is zero if Driver Verifier is enabled.
 * For this reason ExFreeToLookasideListEx always calls L.FreeEx instead of InterlockedPushEntrySList.
 */
_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
NTSTATUS usbip::init_wsk_context_list(_In_ ULONG tag)
{
        if (g_initialized) {
                return STATUS_ALREADY_INITIALIZED;
        }

        g_tag = tag;
        auto err = ExInitializeLookasideListEx(&g_lookaside, allocate_function_ex, free_function_ex, 
                                               NonPagedPoolNx, 0, sizeof(wsk_context), tag, 0);

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
        _In_ device_ctx *dev_ctx, _In_opt_ WDFREQUEST request, _In_ ULONG NumberOfPackets) -> wsk_context*
{
        NT_ASSERT(dev_ctx);

        auto ctx = ::alloc_wsk_context(NumberOfPackets);
        if (ctx) {
                ctx->dev_ctx = dev_ctx;
                ctx->request = request;
        }

        return ctx;
}

/*
 * alloc_wsk_context set ctx->is_isoc, it's safe do not clear it.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::free(_In_opt_ wsk_context *ctx, _In_ bool reuse)
{
        if (!ctx) {
                return;
        }

        ctx->request = WDF_NO_HANDLE;
        ctx->mdl_buf.reset();

        if (reuse) {
                ::reuse(*ctx);
        }

        ExFreeToLookasideListEx(&g_lookaside, ctx);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::prepare_isoc(_In_ wsk_context &ctx, _In_ ULONG NumberOfPackets)
{
        NT_ASSERT(NumberOfPackets != number_of_packets_non_isoch);

        ctx.is_isoc = NumberOfPackets;
        if (!ctx.is_isoc) {
                return STATUS_SUCCESS;
        }

        ULONG isoc_len = NumberOfPackets*sizeof(*ctx.isoc);

        if (ctx.isoc_alloc_cnt < NumberOfPackets) {
                auto isoc = (usbip_iso_packet_descriptor*)ExAllocatePool2(POOL_FLAG_NON_PAGED, isoc_len, g_tag);
                if (!isoc) {
                        return STATUS_INSUFFICIENT_RESOURCES;
                }

                if (ctx.isoc) {
                        ExFreePoolWithTag(ctx.isoc, g_tag);
                }

                ctx.isoc = isoc;
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
