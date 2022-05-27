#include "wsk_data.h"
#include "wsk_cpp.h"
#include "trace.h"
#include "wsk_data.tmh"

#include "dev.h"

namespace
{

inline void assert_wsk_data_offset(_In_ const vpdo_dev_t &vpdo)
{
	if (auto i = vpdo.wsk_data) {
		NT_ASSERT(vpdo.wsk_data_offset < i->Buffer.Length);
	} else {
		NT_ASSERT(!vpdo.wsk_data_offset);
	}
}

} // namespace


void wsk_data_push(_Inout_ vpdo_dev_t &vpdo, _In_ WSK_DATA_INDICATION *DataIndication, _In_ size_t BytesIndicated)
{
	NT_ASSERT(DataIndication);
	NT_ASSERT(wsk::size(DataIndication) == BytesIndicated);

	if (auto &head = vpdo.wsk_data) {
		wsk::get_tail(head)->Next = DataIndication;
	} else {
		head = DataIndication;
		NT_ASSERT(!vpdo.wsk_data_offset);
	}

	TraceWSK("DATA_INDICATION %04x, size %Iu", ptr4log(DataIndication), BytesIndicated);
}

/*
 * Return STATUS_PENDING from WskDisconnectEvent, this function always releases WSK_DATA_INDICATION youself.
 * @return bytes left to consume
 */
size_t wsk_data_consume(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len)
{
	assert_wsk_data_offset(vpdo);
	auto &offset = vpdo.wsk_data_offset;

	auto &head = vpdo.wsk_data;
	auto old_size = wsk::size(head);

	TraceWSK("DATA_INDICATION %04x chain size %Iu bytes, consume %Iu bytes from offset %Iu", 
		  ptr4log(head), old_size, len, offset);

	auto victim = head;
	size_t victim_size = 0;
	int victim_cnt = 0;

	WSK_DATA_INDICATION *prev{};
	for ( ; head && len; prev = head, head = head->Next) {

		const auto &buf = head->Buffer;

		if (buf.Length > offset + len) {
			offset += len; // BBBBBBBBBBB - buffer
			len = 0;       // O...OL...L  - offset, len
			break;
		}

		auto remaining = buf.Length - offset; // BBBBBBBBBB - buffer
		len -= remaining;                     // OOOOOOOOOL...L - max offset, len
		offset = 0;

		victim_size += buf.Length;
		++victim_cnt;
	}

	if (victim != head) {
		if (prev) {
			NT_ASSERT(prev->Next == head);
			prev->Next = nullptr;
		}

		assert_wsk_data_offset(vpdo);
		NT_ASSERT(victim_size == wsk::size(victim));
		NT_ASSERT(victim_size + wsk::size(head) == old_size);

		if (auto err = release(vpdo.sock, victim)) {
			Trace(TRACE_LEVEL_ERROR, "DATA_INDICATION %04x release %!STATUS!", ptr4log(victim), err);
		} else {
			TraceWSK("DATA_INDICATION %04x: %d buffers(%Iu bytes) released, %Iu bytes available from offset %Iu", 
				  ptr4log(victim), victim_cnt, victim_size, old_size - victim_size, offset);
		}
	}

	return len; 
}

size_t wsk_data_size(_In_ const vpdo_dev_t &vpdo)
{
	return wsk::size(vpdo.wsk_data) - vpdo.wsk_data_offset;
}

/*
 * Calls for each usbip_iso_packet_descriptor[] for isoc transfer, do not use logging.
 */
NTSTATUS wsk_data_copy(_In_ const vpdo_dev_t &vpdo, _Out_ void *dest, _In_ size_t offset, _In_ size_t len, _Out_ size_t *actual)
{
	if (actual) {
		*actual = 0;
	}

	offset += vpdo.wsk_data_offset;

	for (auto i = vpdo.wsk_data; i && len; i = i->Next) {

		const auto &buf = i->Buffer;

		if (offset >= buf.Length) {
			offset -= buf.Length;
			continue;
		}

		const ULONG priority = NormalPagePriority | MdlMappingNoWrite | MdlMappingNoExecute;

		auto sysaddr = (char*)MmGetSystemAddressForMdlSafe(buf.Mdl, priority);
		if (!sysaddr) {
			Trace(TRACE_LEVEL_ERROR, "DATA_INDICATION %04x: MmGetSystemAddressForMdlSafe error", ptr4log(i));
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		sysaddr += buf.Offset + offset;
		auto remaining = buf.Length - offset;
		
		auto cnt = min(len, remaining);
		RtlCopyMemory(dest, sysaddr, cnt);

		dest = static_cast<char*>(dest) + cnt;
		len -= cnt;
		if (actual) {
			*actual += cnt;
		}

		if (cnt < remaining) {
			offset += cnt;
		} else {
			offset = 0;
		}
	}

	return len ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}
