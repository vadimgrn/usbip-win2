#include "wsk_data.h"
#include "wsk_cpp.h"
#include "trace.h"
#include "wsk_data.tmh"

#include "dev.h"

namespace
{

auto copy(_Out_ void* &dest, _Inout_ size_t &len, _In_ const WSK_DATA_INDICATION *DataIndication, _In_ size_t offset)
{
	for (auto i = DataIndication; i && len; i = i->Next) {

		auto idx = i - DataIndication;
		auto &buf = i->Buffer;

		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%Id]: length %Iu, offset %Iu, left to copy %Iu", 
			idx, buf.Length, offset, len);

		if (!buf.Length) {
			continue;
		} else if (offset >= buf.Length) {
			offset -= buf.Length;
			continue;
		}

		auto sysaddr = (char*)MmGetSystemAddressForMdlSafe(buf.Mdl, NormalPagePriority | MdlMappingNoWrite | MdlMappingNoExecute);
		if (!sysaddr) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		sysaddr += buf.Offset + offset;
		auto remaining = buf.Length - offset;

		auto cnt = min(len, remaining);
		RtlCopyMemory(dest, sysaddr, cnt);

		dest = static_cast<char*>(dest) + cnt;
		len -= cnt;

		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%Id]: length %Iu, copied %Iu bytes from offset %Iu", 
			idx, buf.Length, cnt, offset);

		offset = 0;
	}

	NT_ASSERT(!offset);
	return STATUS_SUCCESS;
}

} // namespace


bool wsk_data_push(_Inout_ vpdo_dev_t &vpdo, _In_ WSK_DATA_INDICATION *DataIndication, _In_ size_t BytesIndicated)
{
	NT_ASSERT(DataIndication);
	NT_ASSERT(wsk::size(DataIndication) == BytesIndicated);

	auto &i = vpdo.wsk_data_cnt;
	NT_ASSERT(i || !vpdo.wsk_data_offset);

	bool ok = i < ARRAYSIZE(vpdo.wsk_data);
	if (ok) {
		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d] = %p, size %Iu", i, DataIndication, BytesIndicated);
		vpdo.wsk_data[i++] = DataIndication;
		vpdo.wsk_data_release_tail = false;
	}

	return ok;
}

NTSTATUS wsk_data_retain_tail(_Inout_ vpdo_dev_t &vpdo)
{
	if (vpdo.wsk_data_cnt) {
		vpdo.wsk_data_release_tail = true;
		return STATUS_PENDING;
	}

	return STATUS_DATA_NOT_ACCEPTED;
}

bool wsk_data_pop(_Inout_ vpdo_dev_t &vpdo)
{
	NT_ASSERT(!vpdo.wsk_data_offset);
	auto &cnt = vpdo.wsk_data_cnt;

	if (cnt) {
		--cnt;
	} else {
		return false;
	}

	auto victim = *vpdo.wsk_data;

	for (int i = 0; i < cnt; ++i) { // move array elements left
		vpdo.wsk_data[i] = vpdo.wsk_data[i + 1];
	}

	vpdo.wsk_data[cnt] = nullptr;
	NT_ASSERT(bool(cnt) == bool(*vpdo.wsk_data));

	vpdo.wsk_data_offset = 0;

	if (!(cnt || vpdo.wsk_data_release_tail)) {
		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION %p dropped, remaining %d", victim, cnt);
	} else if (auto err = release(vpdo.sock, victim)) {
		Trace(TRACE_LEVEL_ERROR, "WSK_DATA_INDICATION %p release %!STATUS!", victim, err);
	} else {
		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION %p released, remaining %d", victim, cnt);
	}

	if (!cnt) {
		vpdo.wsk_data_release_tail = false;
	}

	return true;
}

void wsk_data_release(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len)
{
	for (int cnt = vpdo.wsk_data_cnt, i = 0; i < cnt && len; ++i) {

		auto di = *vpdo.wsk_data;
		const auto di_len = wsk::size(di);
		auto remaining = di_len - vpdo.wsk_data_offset;

		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d]: size %Iu, offset %Iu, left to release %Iu", 
			i, di_len, vpdo.wsk_data_offset, len);

		if (remaining > len) {
			vpdo.wsk_data_offset += len;
			Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d] retained, size %Iu, offset %Iu", 
				                    i, di_len, vpdo.wsk_data_offset);
			break;
		}

		len -= remaining;
		NT_VERIFY(wsk_data_pop(vpdo));
	}
}

size_t wsk_data_size(_In_ const vpdo_dev_t &vpdo)
{
	size_t total = 0;

	for (int i = 0; i < vpdo.wsk_data_cnt; ++i) {
		total += wsk::size(vpdo.wsk_data[i]);
	}

	return total - vpdo.wsk_data_offset;
}

NTSTATUS wsk_data_copy(_Out_ void *dest, _In_ size_t len, _Inout_ vpdo_dev_t &vpdo)
{
	for (int i = 0; i < vpdo.wsk_data_cnt && len; ++i) {

		auto offset = i ? 0 : vpdo.wsk_data_offset;

		if (auto err = copy(dest, len, vpdo.wsk_data[i], offset)) {
			return err;
		}
	}

	return len ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}
