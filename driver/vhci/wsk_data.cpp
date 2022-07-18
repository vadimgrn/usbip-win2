/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_data.h"
#include "wsk_cpp.h"
#include "trace.h"
#include "wsk_data.tmh"

#include "dev.h"

namespace
{

inline auto check_wsk_data_offset(_In_ const WSK_DATA_INDICATION *di, _In_ size_t offset)
{
	return di ? offset < di->Buffer.Length : !offset;
}

} // namespace


void wsk_data_append(_Inout_ vpdo_dev_t &vpdo, _In_ WSK_DATA_INDICATION *DataIndication, _In_ size_t BytesIndicated)
{
	NT_ASSERT(DataIndication);
	NT_ASSERT(wsk::size(DataIndication) == BytesIndicated);

	if (auto &head = vpdo.wsk_data) {
		NT_ASSERT(vpdo.wsk_data_tail == wsk::tail(head));
		vpdo.wsk_data_tail->Next = DataIndication;
	} else {
		head = DataIndication;
		NT_ASSERT(!vpdo.wsk_data_offset);
	}

	vpdo.wsk_data_tail = wsk::tail(DataIndication);
	TraceWSK("%04x, %Iu bytes", ptr4log(DataIndication), BytesIndicated);
}

/*
 * Return STATUS_PENDING from WskReceiveEvent, this function always releases WSK_DATA_INDICATION youself.
 * @return bytes left to consume
 */
size_t wsk_data_release(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len)
{
	auto &offset = vpdo.wsk_data_offset;

	auto &head = vpdo.wsk_data;
	auto old_size = wsk::size(head);

	auto victim = head;
	size_t victim_size = 0;
	int victim_cnt = 0;

	WSK_DATA_INDICATION *prev{};
	for ( ; head && len; prev = head, head = head->Next) {

		const auto &buf = head->Buffer;

		if (offset + len < buf.Length) {
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
		prev->Next = nullptr;

		NT_ASSERT(victim_size == wsk::size(victim));
		NT_ASSERT(victim_size + wsk::size(head) == old_size);
		auto head_size = old_size - victim_size;

		auto bytes_avail = head_size - offset;
		NT_ASSERT(bytes_avail == wsk_data_size(vpdo));

		if (auto err = release(vpdo.sock, victim)) {
			Trace(TRACE_LEVEL_ERROR, "release(%04x) %!STATUS!", ptr4log(victim), err);
		} else if (head) {
			TraceWSK("Head %04x released -> %d nodes(%Iu bytes), new head %04x -> %Iu/%Iu bytes available from offset %Iu",
				      ptr4log(victim), victim_cnt, victim_size, ptr4log(head), bytes_avail, head_size, offset);
		} else {
			TraceWSK("Head %04x released -> %d nodes(%Iu bytes)", ptr4log(victim), victim_cnt, victim_size);
		}

	} else {
		auto bytes_avail = old_size - offset;
		NT_ASSERT(bytes_avail == wsk_data_size(vpdo));
		TraceWSK("Head %04x -> %Iu/%Iu bytes available from offset %Iu", ptr4log(head), bytes_avail, old_size, offset);
	}

	NT_ASSERT(check_wsk_data_offset(vpdo.wsk_data, vpdo.wsk_data_offset));
	return len;
}

size_t wsk_data_size(_In_ const vpdo_dev_t &vpdo)
{
	return wsk::size(vpdo.wsk_data) - vpdo.wsk_data_offset;
}

/*
 * Calls for each usbip_iso_packet_descriptor[] for isoc transfer, do not use logging.
 */
NTSTATUS wsk_data_copy(
	_In_ const vpdo_dev_t &vpdo, _Out_ void *dest, _In_ size_t offset, _In_ size_t length, _Out_opt_ size_t *actual)
{
	if (actual) {
		*actual = 0;
	}

	offset += vpdo.wsk_data_offset;

	for (auto di = vpdo.wsk_data; di && length; di = di->Next) {

		auto &buf = di->Buffer;
		auto chain_length = buf.Length; 

		if (offset >= chain_length) {
			offset -= chain_length;
			continue;
		}

		auto first_offset = buf.Offset; // within first MDL of the chain

		for (auto mdl = buf.Mdl; mdl && chain_length && length; mdl = mdl->Next, first_offset = 0) { 

			auto mdl_size = MmGetMdlByteCount(mdl);
			auto avail = min(mdl_size - first_offset, chain_length);

			if (offset >= avail) {
				offset -= avail;
				chain_length -= avail;
				continue;
			}

			auto remaining = avail - offset;
			auto cnt = min(remaining, length);

			const ULONG priority = LowPagePriority | MdlMappingNoWrite | MdlMappingNoExecute;

			auto src = (char*)MmGetSystemAddressForMdlSafe(mdl, priority);
			if (!src) {
				Trace(TRACE_LEVEL_ERROR, "MmGetSystemAddressForMdlSafe error");
				return STATUS_INSUFFICIENT_RESOURCES; 
			}

			RtlCopyMemory(dest, src + first_offset + offset, cnt); 

			reinterpret_cast<char*&>(dest) += cnt;
			length -= cnt;
			if (actual) {
				*actual += cnt;
			}

			chain_length -= offset + cnt;

			if (cnt < remaining) {
				offset += cnt;
			} else {
				offset = 0;
			}
		} 
	} 

	return length ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}
