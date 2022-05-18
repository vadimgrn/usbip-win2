#include "csq.h"
#include "trace.h"
#include "csq.tmh"

#include "dev.h"
#include "irp.h"
#include "internal_ioctl.h"

namespace
{

inline auto to_vpdo(IO_CSQ *csq)
{
	return CONTAINING_RECORD(csq, vpdo_dev_t, irps_csq);
}

auto InsertIrp(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID /*InsertContext*/)
{
	auto seqnum = get_seqnum(irp);
	if (!extract_num(seqnum)) {
		TraceCSQ("%04x -> invalid seqnum %u", ptr4log(irp), seqnum);
		return STATUS_INVALID_PARAMETER;
	}

	auto vpdo = to_vpdo(csq);
	InsertTailList(&vpdo->irps, list_entry(irp));

	TraceCSQ("%04x", ptr4log(irp));
	return STATUS_SUCCESS;
}

void RemoveIrp(_In_ IO_CSQ*, _In_ IRP *irp)
{
	TraceCSQ("%04x", ptr4log(irp));
	auto entry = list_entry(irp);
	RemoveEntryList(entry);
	InitializeListHead(entry);
}

auto PeekNextIrp(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID context)
{
	auto vpdo = to_vpdo(csq);
	auto head = &vpdo->irps;

	auto ctx = static_cast<peek_context*>(context);
	IRP *result{};

	for (auto entry = irp ? list_entry(irp)->Flink : head->Flink; entry != head; entry = entry->Flink) {

		auto entry_irp = get_irp(entry);
		bool found = false;

		if (!ctx) {
			found = true;
		} else if (ctx->use_seqnum) {
			found = !ctx->seqnum || ctx->seqnum == get_seqnum(entry_irp);
		} else {
			NT_ASSERT(ctx->handle);
			found = ctx->handle == get_pipe_handle(entry_irp);
		}

		if (found) {
			result = entry_irp;
			break;
		}
	}

	if (!ctx) {
		TraceCSQ("%04x", ptr4log(result));
	} else if (!ctx->use_seqnum) {
		TraceCSQ("PipeHandle %#Ix -> %04x", ph4log(ctx->handle), ptr4log(result));
	} else if (ctx->seqnum) {
		TraceCSQ("seqnum %u -> %04x", ctx->seqnum, ptr4log(result));
	} else {
		TraceCSQ("%04x", ptr4log(result));
	}

	return result;
}

_IRQL_raises_(DISPATCH_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Acquires_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, irps_csq)->irps_lock)
void AcquireLock(_In_ IO_CSQ *csq, _Out_ PKIRQL Irql)
{
	auto vpdo = to_vpdo(csq);
	KeAcquireSpinLock(&vpdo->irps_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, irps_csq)->irps_lock)
void ReleaseLock(_In_ IO_CSQ *csq, _In_ KIRQL Irql)
{
	auto vpdo = to_vpdo(csq);
	KeReleaseSpinLock(&vpdo->irps_lock, Irql);
}

void CompleteCanceledIrp(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	TraceCSQ("%04x", ptr4log(irp));
	auto vpdo = to_vpdo(csq);
	send_cmd_unlink(*vpdo, irp);
}

} // namespace


PAGEABLE NTSTATUS init_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	InitializeListHead(&vpdo.irps);
	KeInitializeSpinLock(&vpdo.irps_lock);

	return IoCsqInitializeEx(&vpdo.irps_csq,
				InsertIrp,
				RemoveIrp,
				PeekNextIrp,
				AcquireLock,
				ReleaseLock,
				CompleteCanceledIrp);
}

void clear_context(IRP *irp)
{
	set_seqnum(irp, 0);
	set_pipe_handle(irp, USBD_PIPE_HANDLE());
}

NTSTATUS enqueue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ IRP *irp)
{
	return IoCsqInsertIrpEx(&vpdo.irps_csq, irp, nullptr, nullptr);
}

IRP *dequeue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ seqnum_t seqnum)
{
	NT_ASSERT(extract_num(seqnum));
	auto ctx = make_peek_context(seqnum);
	return IoCsqRemoveNextIrp(&vpdo.irps_csq, &ctx);
}
