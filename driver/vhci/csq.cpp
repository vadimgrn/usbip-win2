#include "csq.h"
#include "dev.h"
#include "trace.h"
#include "csq.tmh"

#include "irp.h"

namespace
{

inline auto list_entry(IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}

inline auto to_vpdo_read(IO_CSQ *csq)
{
	return CONTAINING_RECORD(csq, vpdo_dev_t, read_irp_queue);
}

inline auto to_vpdo_urb_rx(IO_CSQ *csq)
{
	return CONTAINING_RECORD(csq, vpdo_dev_t, urb_rx_irps_queue);
}

inline auto to_vpdo_urb_tx(IO_CSQ *csq)
{
	return CONTAINING_RECORD(csq, vpdo_dev_t, urb_tx_irps_queue);
}

auto InsertIrp_read(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID InsertContext)
{
	auto vpdo = to_vpdo_read(csq);

	NT_ASSERT(InsertContext);
	auto flags = *static_cast<ULONG*>(InsertContext);

	if ((flags & CSQ_FAIL_IF_URB_PENDING) && !IsListEmpty(&vpdo->urb_rx_irps)) {
		TraceCSQ("%p -> rejected, urb rx irp queue is not empty", irp);
		return STATUS_UNSUCCESSFUL;
	}

	NT_ASSERT(!vpdo->read_irp);
	vpdo->read_irp = irp;

	TraceCSQ("%p", irp);
	return STATUS_SUCCESS;
}

auto InsertIrp_urb_rx(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID InsertContext)
{
	auto vpdo = to_vpdo_urb_rx(csq);
	auto head = &vpdo->urb_rx_irps;

	NT_ASSERT(InsertContext);
	auto &flags = *static_cast<ULONG*>(InsertContext);

	bool tail = flags & CSQ_INSERT_TAIL;
	flags = vpdo->read_irp ? CSQ_READ_PENDING : 0; // OUT

	auto func = tail ? InsertTailList : InsertHeadList;
	func(head, list_entry(irp));

	TraceCSQ("%s %p", tail ? "tail" : "head", irp);
	return STATUS_SUCCESS;
}

void InsertIrp_urb_tx(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	NT_ASSERT(get_seqnum(irp));
	TraceCSQ("%p", irp);

	auto vpdo = to_vpdo_urb_tx(csq);
	InsertTailList(&vpdo->urb_tx_irps, list_entry(irp));
}

void RemoveIrp_read(_In_ IO_CSQ *csq, _In_ IRP *irp)
{
	TraceCSQ("%p", irp);
	auto vpdo = to_vpdo_read(csq);

	NT_ASSERT(vpdo->read_irp == irp);
	vpdo->read_irp = nullptr;
}

void RemoveIrp_urb(_In_ IO_CSQ*, _In_ IRP *irp)
{
	TraceCSQ("%p", irp);
	auto entry = list_entry(irp);
	RemoveEntryList(entry);
	InitializeListHead(entry);
}

auto PeekNextIrp_read(_In_ IO_CSQ *csq, [[maybe_unused]] _In_ IRP *irp, [[maybe_unused]] _In_ PVOID context)
{
	NT_ASSERT(!irp);
	NT_ASSERT(!context);

	auto vpdo = to_vpdo_read(csq);
	TraceCSQ("%p", vpdo->read_irp);
	return vpdo->read_irp;
}

auto PeekNextIrp(_In_ LIST_ENTRY *head, _In_ IRP *irp, _In_ PVOID context)
{
	auto seqnum = as_seqnum(context);
	IRP *result{};

	for (auto entry = irp ? list_entry(irp)->Flink : head->Flink; entry != head; entry = entry->Flink) 
	{
		auto entry_irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);

		if (!seqnum || seqnum == get_seqnum(entry_irp)) {
			result = entry_irp;
			break;
		}
	}

	TraceCSQ("seqnum %u -> %p", seqnum, result);
	return result;
}

auto PeekNextIrp_urb_rx(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID context)
{
	auto vpdo = to_vpdo_urb_rx(csq);
	return PeekNextIrp(&vpdo->urb_rx_irps, irp, context);
}

auto PeekNextIrp_urb_tx(_In_ IO_CSQ *csq, _In_ IRP *irp, _In_ PVOID context)
{
	auto vpdo = to_vpdo_urb_tx(csq);
	return PeekNextIrp(&vpdo->urb_tx_irps, irp, context);
}

_IRQL_raises_(DISPATCH_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Acquires_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, read_irp_queue)->irps_queue_shared_lock)
void AcquireLock_read(_In_ IO_CSQ *csq, _Out_ PKIRQL Irql) 
{
	auto vpdo = to_vpdo_read(csq);
	KeAcquireSpinLock(&vpdo->irps_queue_shared_lock, Irql);
}

_IRQL_raises_(DISPATCH_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Acquires_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, urb_rx_irps_queue)->irps_queue_shared_lock)
void AcquireLock_urb_rx(_In_ IO_CSQ *csq, _Out_ PKIRQL Irql)
{
	auto vpdo = to_vpdo_urb_rx(csq);
	KeAcquireSpinLock(&vpdo->irps_queue_shared_lock, Irql);
}

_IRQL_raises_(DISPATCH_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Acquires_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, urb_tx_irps_queue)->urb_tx_irps_lock)
void AcquireLock_urb_tx(_In_ IO_CSQ *csq, _Out_ PKIRQL Irql)
{
	auto vpdo = to_vpdo_urb_tx(csq);
	KeAcquireSpinLock(&vpdo->urb_tx_irps_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, read_irp_queue)->irps_queue_shared_lock)
void ReleaseLock_read(_In_ IO_CSQ *csq, _In_ KIRQL Irql) 
{
	auto vpdo = to_vpdo_read(csq);
	KeReleaseSpinLock(&vpdo->irps_queue_shared_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, urb_rx_irps_queue)->irps_queue_shared_lock)
void ReleaseLock_urb_rx(_In_ IO_CSQ *csq, _In_ KIRQL Irql)
{
	auto vpdo = to_vpdo_urb_rx(csq);
	KeReleaseSpinLock(&vpdo->irps_queue_shared_lock, Irql);
}

_IRQL_requires_(DISPATCH_LEVEL)
_Releases_lock_(CONTAINING_RECORD(csq, vpdo_dev_t, urb_tx_irps_queue)->urb_tx_irps_lock)
void ReleaseLock_urb_tx(_In_ IO_CSQ *csq, _In_ KIRQL Irql)
{
	auto vpdo = to_vpdo_urb_tx(csq);
	KeReleaseSpinLock(&vpdo->urb_tx_irps_lock, Irql);
}

void CompleteCanceledIrp(_In_ IO_CSQ*, _In_ IRP *irp)
{
	complete_canceled_irp(irp);
}

PAGEABLE auto init_read_irp_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	return IoCsqInitializeEx(&vpdo.read_irp_queue,
				InsertIrp_read,
				RemoveIrp_read,
				PeekNextIrp_read,
				AcquireLock_read,
				ReleaseLock_read,
				CompleteCanceledIrp);
}

PAGEABLE auto init_urb_rx_irps_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	InitializeListHead(&vpdo.urb_rx_irps);

	return IoCsqInitializeEx(&vpdo.urb_rx_irps_queue,
				InsertIrp_urb_rx,
				RemoveIrp_urb,
				PeekNextIrp_urb_rx,
				AcquireLock_urb_rx,
				ReleaseLock_urb_rx,
				CompleteCanceledIrp);
}

PAGEABLE auto init_urb_tx_irps_queue(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	InitializeListHead(&vpdo.urb_tx_irps);

	return IoCsqInitialize(&vpdo.urb_tx_irps_queue,
				InsertIrp_urb_tx,
				RemoveIrp_urb,
				PeekNextIrp_urb_tx,
				AcquireLock_urb_tx,
				ReleaseLock_urb_tx,
				CompleteCanceledIrp);
}

} // namespace


PAGEABLE NTSTATUS init_queues(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	using func = NTSTATUS(vpdo_dev_t&);
	func* v[] { init_read_irp_queue, init_urb_rx_irps_queue, init_urb_tx_irps_queue };

	for (auto f: v) {
		if (auto err = f(vpdo)) {
			return err;
		}
	}

	KeInitializeSpinLock(&vpdo.irps_queue_shared_lock);
	return STATUS_SUCCESS;
}
