/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_context.h"
#include "trace.h"
#include "wsk_context.tmh"

#include "dev.h"

namespace
{

const ULONG AllocTag = 'LKSW';

_IRQL_requires_same_
_Function_class_(free_function_ex)
void free_function_ex(_In_ __drv_freesMem(Mem) void *Buffer, _Inout_ LOOKASIDE_LIST_EX*)
{
        auto ctx = static_cast<wsk_context*>(Buffer);
        NT_ASSERT(ctx);

        TraceWSK("%04x, isoc[%Iu]", ptr4log(ctx), ctx->isoc_alloc_cnt);

        ctx->mdl_hdr.reset();
        ctx->mdl_buf.reset();
        ctx->mdl_isoc.reset();

        if (auto irp = ctx->wsk_irp) {
                IoFreeIrp(irp);
        }

        if (auto ptr = ctx->isoc) {
                ExFreePoolWithTag(ptr, AllocTag);
        }

        ExFreePoolWithTag(ctx, AllocTag);
}

_IRQL_requires_same_
_Function_class_(allocate_function_ex)
void *allocate_function_ex(_In_ [[maybe_unused]] POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes, _In_ ULONG Tag, _Inout_ LOOKASIDE_LIST_EX *list)
{
        NT_ASSERT(PoolType == NonPagedPoolNx);
        NT_ASSERT(Tag == AllocTag);

        auto ctx = (wsk_context*)ExAllocatePool2(POOL_FLAG_NON_PAGED, NumberOfBytes, Tag);
        if (!ctx) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", NumberOfBytes);
                return nullptr;
        }

        ctx->mdl_hdr = usbip::Mdl(usbip::memory::nonpaged, &ctx->hdr, sizeof(ctx->hdr));

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

        TraceWSK("-> %04x", ptr4log(ctx));
        return ctx;
}

} // namespace


/*
 * LOOKASIDE_LIST_EX.L.Depth is zero if Driver Verifier is enabled.
 * For this reason ExFreeToLookasideListEx always calls L.FreeEx instead of InterlockedPushEntrySList.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS init_wsk_context_list()
{
        return ExInitializeLookasideListEx(&wsk_context_list, allocate_function_ex, free_function_ex, 
                                            NonPagedPoolNx, 0, sizeof(wsk_context), AllocTag, 0);
}

/*
 * If use ExFreeToLookasideListEx in case of error, next ExAllocateFromLookasideListEx will return the same pointer.
 * free_function_ex is used instead in hope that next object in the LookasideList may have required buffer.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
wsk_context *alloc_wsk_context(_In_ ULONG NumberOfPackets)
{
        auto ctx = (wsk_context*)ExAllocateFromLookasideListEx(&wsk_context_list);

        if (!ctx) {
                Trace(TRACE_LEVEL_ERROR, "ExAllocateFromLookasideListEx error");
        } else if (auto err = prepare_isoc(*ctx, NumberOfPackets)) {
                Trace(TRACE_LEVEL_ERROR, "prepare_isoc(NumberOfPackets %lu) %!STATUS!", NumberOfPackets, err);
                free_function_ex(ctx, &wsk_context_list);
                ctx = nullptr;
        }

        return ctx;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS prepare_isoc(_In_ wsk_context &ctx, _In_ ULONG NumberOfPackets)
{
        if (!(ctx.is_isoc = NumberOfPackets)) { // assignment
                return STATUS_SUCCESS;
        }

        ULONG isoc_len = NumberOfPackets*sizeof(*ctx.isoc);

        if (ctx.isoc_alloc_cnt < NumberOfPackets) {
                auto isoc = (usbip_iso_packet_descriptor*)ExAllocatePool2(POOL_FLAG_NON_PAGED, isoc_len, AllocTag);
                if (!isoc) {
                        return STATUS_INSUFFICIENT_RESOURCES;
                }

                if (ctx.isoc) {
                        ExFreePoolWithTag(ctx.isoc, AllocTag);
                }

                ctx.isoc = isoc;
                ctx.isoc_alloc_cnt = NumberOfPackets;

                ctx.mdl_isoc.reset();
        }

        if (ctx.mdl_isoc.size() != isoc_len) {
                ctx.mdl_isoc = usbip::Mdl(usbip::memory::nonpaged, ctx.isoc, isoc_len);
                if (auto err = ctx.mdl_isoc.prepare_nonpaged()) {
                        return err;
                }
                NT_ASSERT(number_of_packets(ctx) == NumberOfPackets);
        }

        return STATUS_SUCCESS;
}

/*
 * alloc_wsk_context set ctx->is_isoc, it's safe do not clear it.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
void free(_In_opt_ wsk_context *ctx, _In_ bool reuse)
{
        if (!ctx) {
                return;
        }

        ctx->vpdo = nullptr;
        ctx->irp = nullptr;
        ctx->mdl_buf.reset();

        if (reuse) {
                ::reuse(*ctx);
        }

        ExFreeToLookasideListEx(&wsk_context_list, ctx);
}
