#include "send_context.h"
#include "trace.h"
#include "send_context.tmh"

#include "dev.h"

LOOKASIDE_LIST_EX send_context_list;

namespace
{

const ULONG AllocTag = 'LKSW';

_IRQL_requires_same_
_Function_class_(allocate_function_ex)
void *allocate_function_ex(_In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes, _In_ ULONG Tag, _Inout_ LOOKASIDE_LIST_EX*)
{
        NT_ASSERT(PoolType == NonPagedPool);
        NT_ASSERT(Tag == AllocTag);

        auto ctx = (send_context*)ExAllocatePool2(POOL_FLAG_NON_PAGED, NumberOfBytes, Tag);
        if (!ctx) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", NumberOfBytes);
                return nullptr;
        }

        ctx->mdl_hdr = usbip::Mdl(usbip::memory::nonpaged, &ctx->hdr, sizeof(ctx->hdr));

        if (auto err = ctx->mdl_hdr.prepare_nonpaged()) {
                Trace(TRACE_LEVEL_ERROR, "mdl_hdr %!STATUS!", err);
                ExFreePoolWithTag(ctx, Tag);
                return nullptr;
        }

        ctx->wsk_irp = IoAllocateIrp(1, false);
        if (!ctx->wsk_irp) {
                Trace(TRACE_LEVEL_ERROR, "IoAllocateIrp -> NULL");
                ctx->mdl_hdr.reset();
                ExFreePoolWithTag(ctx, Tag);
                return nullptr;
        }

        TraceWSK("-> %04x", ptr4log(ctx));
        return ctx;
}

_IRQL_requires_same_
_Function_class_(free_function_ex)
void free_function_ex(_In_ __drv_freesMem(Mem) void *Buffer, _Inout_ LOOKASIDE_LIST_EX*)
{
        auto ctx = static_cast<send_context*>(Buffer);
        NT_ASSERT(ctx);

        TraceWSK("%04x, number of packets %Iu, allocated %lu", ptr4log(ctx), number_of_packets(*ctx), ctx->isoc_alloc_cnt);

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

} // namespace


NTSTATUS init_send_context_list()
{
        return ExInitializeLookasideListEx(&send_context_list, allocate_function_ex, free_function_ex, 
                                            NonPagedPool, 0, sizeof(send_context), AllocTag, 0);
}

/*
 * If use ExFreeToLookasideListEx in case of error, next ExAllocateFromLookasideListEx will return the same pointer.
 * free_function_ex is used instead in hope that next object in the LookasideList may have required buffer.
 */
send_context *alloc_send_context(_In_ ULONG NumberOfPackets)
{
        auto ctx = (send_context*)ExAllocateFromLookasideListEx(&send_context_list);
        if (!ctx) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate context");
                return ctx;
        }
        
        if (!(ctx->is_isoc = NumberOfPackets)) { // assignment
                return ctx;
        }

        ULONG isoc_len = NumberOfPackets*sizeof(*ctx->isoc);

        if (ctx->isoc_alloc_cnt < NumberOfPackets) {
                auto isoc = (usbip_iso_packet_descriptor*)ExAllocatePool2(POOL_FLAG_NON_PAGED, isoc_len, AllocTag);
                if (!isoc) {
                        Trace(TRACE_LEVEL_ERROR, "Can't allocate usbip_iso_packet_descriptor[%lu]", NumberOfPackets);
                        free_function_ex(ctx, &send_context_list);
                        return nullptr;
                }

                if (ctx->isoc) {
                        ExFreePoolWithTag(ctx->isoc, AllocTag);
                }

                ctx->isoc = isoc;
                ctx->isoc_alloc_cnt = NumberOfPackets;

                ctx->mdl_isoc.reset();
        }

        if (ctx->mdl_isoc.size() != isoc_len) {
                ctx->mdl_isoc = usbip::Mdl(usbip::memory::nonpaged, ctx->isoc, isoc_len);
                if (auto err = ctx->mdl_isoc.prepare_nonpaged()) {
                        Trace(TRACE_LEVEL_ERROR, "mdl_isoc %!STATUS!", err);
                        free_function_ex(ctx, &send_context_list);
                        return nullptr;
                }
                NT_ASSERT(number_of_packets(*ctx) == NumberOfPackets);
        }

        return ctx;
}

void free(_In_ send_context *ctx)
{
        if (ctx) {
                ctx->mdl_buf.reset();
                IoReuseIrp(ctx->wsk_irp, STATUS_UNSUCCESSFUL);
                ExFreeToLookasideListEx(&send_context_list, ctx);
        }
}

