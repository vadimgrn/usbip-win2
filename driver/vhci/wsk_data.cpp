#include "wsk_data.h"
#include "wsk_cpp.h"
#include "trace.h"
#include "wsk_data.tmh"

#include "dev.h"

namespace
{

auto copy(_Out_ void* &dest, _Inout_ size_t &len, 
	_In_ int index, _In_ const WSK_DATA_INDICATION *DataIndication, _In_ size_t offset)
{
	int j = 0;

	for (auto i = DataIndication; i && len; i = i->Next, ++j) {

		auto &buf = i->Buffer;

		if (!buf.Length) {
			continue;
		} else if (offset >= buf.Length) {
			Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d][%d]: buffer length %Iu, offset %Iu, skipped, left to copy %Iu",
				                    index, j, buf.Length, offset, len);
			offset -= buf.Length;
			continue;
		}

		const ULONG priority = NormalPagePriority | MdlMappingNoWrite | MdlMappingNoExecute;

		auto sysaddr = (char*)MmGetSystemAddressForMdlSafe(buf.Mdl, priority);
		if (!sysaddr) {
			Trace(TRACE_LEVEL_ERROR, "WSK_DATA_INDICATION[%d][%d]: MmGetSystemAddressForMdlSafe error", index, j);
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		sysaddr += buf.Offset + offset;
		auto remaining = buf.Length - offset;

		auto cnt = min(len, remaining);

		RtlCopyMemory(dest, sysaddr, cnt);
		dest = static_cast<char*>(dest) + cnt;
		len -= cnt;

		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d][%d]: buffer length %Iu, "
					   "%Iu bytes %s from offset %Iu, left to copy %Iu", 
			                    index, j, buf.Length, cnt, dest ? "copied" : "skipped", offset, len);

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

	auto &cnt = vpdo.wsk_data_cnt;
	NT_ASSERT(cnt || !vpdo.wsk_data_offset);

	bool ok = cnt < ARRAYSIZE(vpdo.wsk_data);
	if (ok) {
		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d] = %04x, size %Iu", cnt, ptr4log(DataIndication), BytesIndicated);
		vpdo.wsk_data[cnt++] = DataIndication;
	}

	return ok;
}

bool wsk_data_pop(_Inout_ vpdo_dev_t &vpdo, _In_ bool skip_release_back)
{
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

	if (!cnt && skip_release_back) {
		Trace(TRACE_LEVEL_ERROR, "WSK_DATA_INDICATION %04x skipped", ptr4log(victim));
	} else if (auto err = release(vpdo.sock, victim)) {
		Trace(TRACE_LEVEL_ERROR, "WSK_DATA_INDICATION %04x release %!STATUS!", ptr4log(victim), err);
	} else {
		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION %04x released, remaining %d", ptr4log(victim), cnt);
	}

	return true;
}

void wsk_data_consume(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len)
{
	for (int cnt = vpdo.wsk_data_cnt, i = 0; i < cnt && len; ++i) {

		auto di = *vpdo.wsk_data;
		const auto di_len = wsk::size(di);
		auto remaining = di_len - vpdo.wsk_data_offset;

		Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d]: size %Iu, offset %Iu, left to consume %Iu", 
			i, di_len, vpdo.wsk_data_offset, len);

		if (remaining > len) {
			vpdo.wsk_data_offset += len;
			Trace(TRACE_LEVEL_VERBOSE, "WSK_DATA_INDICATION[%d] retained, size %Iu, offset %Iu", 
				                    i, di_len, vpdo.wsk_data_offset);
			break;
		}

		len -= remaining;
		NT_VERIFY(wsk_data_pop(vpdo, true));
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

NTSTATUS wsk_data_copy(_Inout_ vpdo_dev_t &vpdo, _Out_ void *dest, _In_ size_t len)
{
	for (int i = 0; i < vpdo.wsk_data_cnt && len; ++i) {

		auto offset = i ? 0 : vpdo.wsk_data_offset;

		if (auto err = copy(dest, len, i, vpdo.wsk_data[i], offset)) {
			return err;
		}
	}

	return len ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

WSK_DATA_INDICATION *wsk_data_back(_In_ const vpdo_dev_t &vpdo)
{
	auto cnt = vpdo.wsk_data_cnt;
	return cnt ? vpdo.wsk_data[cnt - 1] : nullptr;
}
