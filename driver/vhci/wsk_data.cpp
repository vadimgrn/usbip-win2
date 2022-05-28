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
	auto &offset = vpdo.wsk_data_offset;

	auto &head = vpdo.wsk_data;
	auto old_size = wsk::size(head);

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

		NT_ASSERT(victim_size == wsk::size(victim));
		NT_ASSERT(victim_size + wsk::size(head) == old_size);

		if (auto err = release(vpdo.sock, victim)) {
			Trace(TRACE_LEVEL_ERROR, "DATA_INDICATION %04x release %!STATUS!", ptr4log(victim), err);
		} else {
			TraceWSK("DATA_INDICATION %04x: %d buffers(%Iu bytes) released, %Iu bytes available from offset %Iu", 
				  ptr4log(victim), victim_cnt, victim_size, old_size - victim_size, offset);
		}
	} else {
		TraceWSK("DATA_INDICATION %04x: %Iu bytes available from offset %Iu", ptr4log(head), old_size - offset, offset);
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
 * 
 * @param offset to copy from, will be ignored for each next call if consume is used
 * @param consume resume copying from the last position, the same effect as if call wsk_data_consume after wsk_data_copy,
 *      but the internal state is saved in this parameter and wsk_data/wsk_data_offset are not affected
 */
NTSTATUS wsk_data_copy(
	_In_ const vpdo_dev_t &vpdo, _Out_ void *dest, _In_ size_t offset, _In_ size_t len, 
	_Inout_ WskDataCopyState *consume, _Out_ size_t *actual)
{
	if (actual) {
		*actual = 0;
	}

	auto cur = vpdo.wsk_data;

	if (consume && consume->next) {
		if (!check_wsk_data_offset(consume->cur, consume->offset)) {
			return STATUS_INVALID_PARAMETER;
		}
		cur = consume->cur;
		offset = consume->offset;
	} else {
		offset += vpdo.wsk_data_offset;
	}

	for ( ; cur && len; cur = cur->Next) {

		const auto &buf = cur->Buffer;

		if (offset >= buf.Length) {
			offset -= buf.Length;
			continue;
		}

		const ULONG priority = NormalPagePriority | MdlMappingNoWrite | MdlMappingNoExecute;

		auto sysaddr = (char*)MmGetSystemAddressForMdlSafe(buf.Mdl, priority);
		if (!sysaddr) {
			Trace(TRACE_LEVEL_ERROR, "DATA_INDICATION %04x: MmGetSystemAddressForMdlSafe error", ptr4log(cur));
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

		if (cnt < remaining) { // BBBBBBBBBB - buffer
			offset += cnt; // OOOOL...L  - offset, len
			break;
		} else {               // BBBBBBBBBB - buffer
			offset = 0;    // OOOOOOOOOL...L - max offset, len
		}
	}

	if (consume) {
		consume->cur = cur;
		consume->offset = offset;
		consume->next = true;
	}
	
	NT_ASSERT(check_wsk_data_offset(consume ? consume->cur : cur,
		                        consume ? consume->offset : offset));

	return len ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}
