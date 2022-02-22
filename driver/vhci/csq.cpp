#include "csq.h"
#include "dev.h"
#include "trace.h"
#include "csq.tmh"

#include "irp.h"
#include "read.h"

namespace
{

inline auto to_vpdo_read(IO_CSQ *csq)
{
	return CONTAINING_RECORD(csq, vpdo_dev_t, read_irp_csq);
}

inline auto to_vpdo_rx(IO_CSQ *csq)
{
	return CONTAINING_RECORD(csq, vpdo_dev_t, rx_irps_csq);
}

inline auto to_vpdo_tx(IO_CSQ *csq)
{
	return CONTAINING_RECORD(csq, vpdo_dev_t, tx_irps_csq);
}

auto InsertIrp_read(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID InsertContext)
{
	NTSTATUS err{};
	auto vpdo = to_vpdo_read(csq);
	bool check_rx = InsertContext == InsertIfRxEmpty();

	KIRQL irql{};
	if (check_rx) {
		KeAcquireSpinLock(&vpdo->rx_lock, &irql);
	}
		
	if (!check_rx || (IsListEmpty(&vpdo->rx_irps) && IsListEmpty(&vpdo->rx_canceled_irps))) {
		auto old_ptr = InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vpdo->read_irp), irp);
		NT_ASSERT(!old_ptr);
	} else {
		err = STATUS_UNSUCCESSFUL;
	}

	if (check_rx) {
		KeReleaseSpinLock(&vpdo->rx_lock, irql);
	}

	TraceCSQ("%04x %!STATUS!", irp4log(irp), err);
	return err;
}

auto InsertIrp_rx(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID InsertContext)
{
	auto vpdo = to_vpdo_rx(csq);
	bool tail = InsertContext == InsertTail();

	auto func = tail ? InsertTailList : InsertHeadList;
	func(&vpdo->rx_irps, list_entry(irp));

	TraceCSQ("%04x -> %s ", irp4log(irp), tail ? "tail" : "head");
	return STATUS_SUCCESS;
}

void InsertIrp_tx(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	NT_ASSERT(get_seqnum(irp));
	TraceCSQ("%04x", irp4log(irp));

	auto vpdo = to_vpdo_tx(csq);
	InsertTailList(&vpdo->tx_irps, list_entry(irp));
}

void RemoveIrp_read(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	TraceCSQ("%04x", irp4log(irp));
	auto vpdo = to_vpdo_read(csq);

	auto old_ptr = InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vpdo->read_irp), nullptr);
	NT_ASSERT(old_ptr == irp);
}

void RemoveIrp(_In_ IO_CSQ*, _In_ IRP *irp)
{
	TraceCSQ("%04x", irp4log(irp));
	auto entry = list_entry(irp);
	RemoveEntryList(entry);
	InitializeListHead(entry);
}

auto PeekNextIrp_read(_In_ IO_CSQ *csq, [[maybe_unused]] _In_ IRP *irp, [[maybe_unused]] _In_ PVOID context)
{
	NT_ASSERT(!irp);
	NT_ASSERT(!context);

	auto vpdo = to_vpdo_read(csq);
	auto ptr = *reinterpret_cast<IRP* volatile *>(&vpdo->read_irp); // dereference pointer to volatile pointer

	TraceCSQ("%04x", irp4log(ptr));
	return ptr;
}

auto PeekNextIrp(_In_ LIST_ENTRY *head, _In_ IRP *irp, _In_ PVOID context)
{
	auto ctx = static_cast<peek_context*>(context);
	IRP *result{};

	for (auto entry = irp ? list_entry(irp)->Flink : head->Flink; entry != head; entry = entry->Flink) {

		auto entry_irp = get_irp(entry);
		bool found = false;
		
		if (!ctx) {
			found = true;
		} else if (ctx->use_seqnum) {
			found = !ctx->u.seqnum || ctx->u.seqnum == get_seqnum(entry_irp);
		} else {
			NT_ASSERT(ctx->u.handle);
			found = ctx->u.handle == get_pipe_handle(entry_irp);
		}

		if (found) {
			result = entry_irp;
			break;
		}
	}

	if (!ctx) {
		TraceCSQ("%04x", irp4log(result));
	} else if (!ctx->use_seqnum) {
		TraceCSQ("PipeHandle %#Ix -> %04x", ph4log(ctx->u.handle), irp4log(result));
	} else if (ctx->u.seqnum) {
		TraceCSQ("seqnum %u -> %04x", ctx->u.seqnum, irp4log(result));
	} else {
		TraceCSQ("%04x", irp4log(result));
	}

	return result;
}

auto PeekNextIrp_rx(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID context)
{
	auto vpdo = to_vpdo_rx(csq);
	return PeekNextIrp(&vpdo->rx_irps, irp, context);
}

auto PeekNextIrp_tx(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID context)
{
	auto vpdo = to_vpdo_tx(csq);
	return PeekNextIrp(&vpdo->tx_irps, irp, context);
}

// atomic operations are used
void AcquireLock_read(_In_ IO_CSQ*, _Out_ PKIRQL) {}
void ReleaseLock_read(_In_ IO_CSQ*, _In_ KIRQL) {}

_IRQL_raises_(DISPATCH_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Acquires_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, rx_irps_csq)->rx_lock)
void AcquireLock_rx(_In_ IO_CSQ *csq, _Out_ PKIRQL Irql)
{
	auto vpdo = to_vpdo_rx(csq);
	KeAcquireSpinLock(&vpdo->rx_lock, Irql);
}

_IRQL_raises_(DISPATCH_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Acquires_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, tx_irps_csq)->tx_irps_lock)
void AcquireLock_tx(_In_ IO_CSQ *csq, _Out_ PKIRQL Irql)
{
	auto vpdo = to_vpdo_tx(csq);
	KeAcquireSpinLock(&vpdo->tx_irps_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, rx_irps_csq)->rx_lock)
void ReleaseLock_rx(_In_ IO_CSQ *csq, _In_ KIRQL Irql)
{
	auto vpdo = to_vpdo_rx(csq);
	KeReleaseSpinLock(&vpdo->rx_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, tx_irps_csq)->tx_irps_lock)
void ReleaseLock_tx(_In_ IO_CSQ *csq, _In_ KIRQL Irql)
{
	auto vpdo = to_vpdo_tx(csq);
	KeReleaseSpinLock(&vpdo->tx_irps_lock, Irql);
}

void CompleteCanceledIrp_read(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	auto vpdo = to_vpdo_read(csq);
	complete_canceled_irp(vpdo, irp);
}

void CompleteCanceledIrp_rx(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	auto vpdo = to_vpdo_rx(csq);
	complete_canceled_irp(vpdo, irp); // was not sent to server
}

void CompleteCanceledIrp_tx(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	TraceCSQ("%04x", irp4log(irp));
	auto vpdo = to_vpdo_tx(csq);
	send_cmd_unlink(vpdo, irp);
}

PAGEABLE auto init_read_irp_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	return IoCsqInitializeEx(&vpdo.read_irp_csq,
				InsertIrp_read,
				RemoveIrp_read,
				PeekNextIrp_read,
				AcquireLock_read,
				ReleaseLock_read,
				CompleteCanceledIrp_read);
}

PAGEABLE auto init_rx_irps_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	InitializeListHead(&vpdo.rx_irps);
	KeInitializeSpinLock(&vpdo.rx_lock);

	return IoCsqInitializeEx(&vpdo.rx_irps_csq,
				InsertIrp_rx,
				RemoveIrp,
				PeekNextIrp_rx,
				AcquireLock_rx,
				ReleaseLock_rx,
				CompleteCanceledIrp_rx);
}

PAGEABLE auto init_tx_irps_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	InitializeListHead(&vpdo.tx_irps);
	KeInitializeSpinLock(&vpdo.tx_irps_lock);

	return IoCsqInitialize(&vpdo.tx_irps_csq,
				InsertIrp_tx,
				RemoveIrp,
				PeekNextIrp_tx,
				AcquireLock_tx,
				ReleaseLock_tx,
				CompleteCanceledIrp_tx);
}

} // namespace


PAGEABLE NTSTATUS init_queues(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	using func = NTSTATUS(vpdo_dev_t&);
	func* v[] { init_read_irp_queue, init_rx_irps_queue, init_tx_irps_queue };

	for (auto f: v) {
		if (auto err = f(vpdo)) {
			return err;
		}
	}

	return STATUS_SUCCESS;
}
