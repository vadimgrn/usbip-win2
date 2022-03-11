#include "csq.h"
#include "trace.h"
#include "csq.tmh"

#include "dev.h"
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

/*
 * @see read.cpp, dequeue_rx_irp.
 */
inline auto rx_unlink_unavail(vpdo_dev_t *vpdo)
{
	return  vpdo->seqnum_payload || // can't interrupt reading of payload
		IsListEmpty(&vpdo->rx_irps_unlink);
}

auto InsertIrp_read(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID InsertContext)
{
	NTSTATUS err{};
	auto vpdo = to_vpdo_read(csq);
	bool check_rx = InsertContext == InsertIfRxEmpty();

	KIRQL irql{};
	if (check_rx) {
		KeAcquireSpinLock(&vpdo->rx_irps_lock, &irql);
	}

	if (check_rx && !(IsListEmpty(&vpdo->rx_irps) && rx_unlink_unavail(vpdo))) {
		err = STATUS_UNSUCCESSFUL;
	} else {
		auto old_ptr = InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vpdo->read_irp), irp);
		NT_ASSERT(!old_ptr);
	}

	if (check_rx) {
		KeReleaseSpinLock(&vpdo->rx_irps_lock, irql);
	}

	TraceCSQ("%04x%s", ptr4log(irp), err ? " not inserted, rx irp is available" : " ");
	return err;
}

auto InsertIrp_rx(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID InsertContext)
{
	auto vpdo = to_vpdo_rx(csq);
	bool tail = InsertContext == InsertTail();

	auto func = tail ? InsertTailList : InsertHeadList;
	func(&vpdo->rx_irps, list_entry(irp));

	TraceCSQ("%04x -> %s ", ptr4log(irp), tail ? "tail" : "head");
	return STATUS_SUCCESS;
}

void InsertIrp_tx(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	NT_ASSERT(get_seqnum(irp));
	TraceCSQ("%04x", ptr4log(irp));

	auto vpdo = to_vpdo_tx(csq);
	InsertTailList(&vpdo->tx_irps, list_entry(irp));
}

void RemoveIrp_read(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	TraceCSQ("%04x", ptr4log(irp));
	auto vpdo = to_vpdo_read(csq);

	auto old_ptr = InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vpdo->read_irp), nullptr);
	NT_ASSERT(old_ptr == irp);
}

void RemoveIrp(_In_ IO_CSQ*, _In_ IRP *irp)
{
	TraceCSQ("%04x", ptr4log(irp));
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

	TraceCSQ("%04x", ptr4log(ptr));
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
		TraceCSQ("%04x", ptr4log(result));
	} else if (!ctx->use_seqnum) {
		TraceCSQ("PipeHandle %#Ix -> %04x", ph4log(ctx->u.handle), ptr4log(result));
	} else if (ctx->u.seqnum) {
		TraceCSQ("seqnum %u -> %04x", ctx->u.seqnum, ptr4log(result));
	} else {
		TraceCSQ("%04x", ptr4log(result));
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
_Acquires_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, rx_irps_csq)->rx_irps_lock)
void AcquireLock_rx(_In_ IO_CSQ *csq, _Out_ PKIRQL Irql)
{
	auto vpdo = to_vpdo_rx(csq);
	KeAcquireSpinLock(&vpdo->rx_irps_lock, Irql);
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
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, rx_irps_csq)->rx_irps_lock)
void ReleaseLock_rx(_In_ IO_CSQ *csq, _In_ KIRQL Irql)
{
	auto vpdo = to_vpdo_rx(csq);
	KeReleaseSpinLock(&vpdo->rx_irps_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, tx_irps_csq)->tx_irps_lock)
void ReleaseLock_tx(_In_ IO_CSQ *csq, _In_ KIRQL Irql)
{
	auto vpdo = to_vpdo_tx(csq);
	KeReleaseSpinLock(&vpdo->tx_irps_lock, Irql);
}

void CompleteCanceledIrp(_In_ IO_CSQ*, _In_ IRP *irp)
{
	complete_canceled_irp(irp); // was not sent to server
}

void CompleteCanceledIrp_tx(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	TraceCSQ("%04x", ptr4log(irp));
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
				CompleteCanceledIrp);
}

PAGEABLE auto init_rx_irps_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	InitializeListHead(&vpdo.rx_irps);
	InitializeListHead(&vpdo.rx_irps_unlink);
	KeInitializeSpinLock(&vpdo.rx_irps_lock);

	return IoCsqInitializeEx(&vpdo.rx_irps_csq,
				InsertIrp_rx,
				RemoveIrp,
				PeekNextIrp_rx,
				AcquireLock_rx,
				ReleaseLock_rx,
				CompleteCanceledIrp);
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

void enqueue_rx_unlink_irp(vpdo_dev_t *vpdo, IRP *irp)
{
	NT_ASSERT(get_seqnum_unlink(irp));
	TraceCSQ("irp %04x, unlink seqnum %u", ptr4log(irp), get_seqnum_unlink(irp));

	KIRQL irql;
	KeAcquireSpinLock(&vpdo->rx_irps_lock, &irql);
	InsertTailList(&vpdo->rx_irps_unlink, list_entry(irp));
	KeReleaseSpinLock(&vpdo->rx_irps_lock, irql);
}

IRP *dequeue_rx_unlink_irp(vpdo_dev_t *vpdo, seqnum_t seqnum_unlink)
{
	KIRQL irql;
	KeAcquireSpinLock(&vpdo->rx_irps_lock, &irql);
	auto irp = dequeue(&vpdo->rx_irps_unlink, seqnum_unlink, true);
	KeReleaseSpinLock(&vpdo->rx_irps_lock, irql);		

	TraceCSQ("unlink seqnum %u -> irp %04x", seqnum_unlink, ptr4log(irp));
	return irp;
}

void clear_context(IRP *irp, bool skip_unlink)
{
	set_seqnum(irp, 0);

	if (!skip_unlink) {
		set_seqnum_unlink(irp, 0);
	}

	set_pipe_handle(irp, USBD_PIPE_HANDLE());
}
