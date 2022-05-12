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

auto InsertIrp(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID InsertContext)
{
	auto seqnum = get_seqnum(irp);
	if (!seqnum) {
		TraceCSQ("%04x -> seqnum is not set", ptr4log(irp));
		return STATUS_INVALID_PARAMETER;
	}

	auto vpdo = to_vpdo(csq);
	bool tail = InsertContext == InsertTail();

	auto func = tail ? InsertTailList : InsertHeadList;
	func(&vpdo->irps, list_entry(irp));

	TraceCSQ("%04x(seqnum %u) -> %s ", ptr4log(irp), seqnum, tail ? "tail" : "head");
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
	send_cmd_unlink(vpdo, irp);
}

auto dequeue(LIST_ENTRY *head, seqnum_t seqnum, bool unlink)
{
	auto func = unlink ? get_seqnum_unlink : get_seqnum;
	IRP *irp{};

	for (auto entry = head->Flink; entry != head; entry = entry->Flink) {

		auto entry_irp = get_irp(entry);

		if (!seqnum || seqnum == func(entry_irp)) {
			RemoveEntryList(entry);
			InitializeListHead(entry);
			irp = entry_irp;
			break;
		}
	}

	return irp;
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

void enqueue_unlink_irp(vpdo_dev_t*, IRP *irp)
{
	NT_ASSERT(get_seqnum_unlink(irp));
	TraceCSQ("irp %04x, unlink seqnum %u", ptr4log(irp), get_seqnum_unlink(irp));

//	KIRQL irql;
//	KeAcquireSpinLock(&vpdo->irps_lock, &irql);
//	InsertTailList(&vpdo->rx_irps_unlink, list_entry(irp));
//	KeReleaseSpinLock(&vpdo->rx_irps_lock, irql);
}

IRP *dequeue_unlink_irp(vpdo_dev_t*, seqnum_t /*seqnum_unlink*/)
{
//	KIRQL irql;
//	KeAcquireSpinLock(&vpdo->irps_lock, &irql);
//	auto irp = dequeue(&vpdo->irps_unlink, seqnum_unlink, true);
//	KeReleaseSpinLock(&vpdo->irps_lock, irql);		

//	TraceCSQ("unlink seqnum %u -> irp %04x", seqnum_unlink, ptr4log(irp));
	return nullptr;
}

void clear_context(IRP *irp, bool skip_unlink)
{
	set_seqnum(irp, 0);

	if (!skip_unlink) {
		set_seqnum_unlink(irp, 0);
	}

	set_pipe_handle(irp, USBD_PIPE_HANDLE());
}
