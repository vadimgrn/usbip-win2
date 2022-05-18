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
			TraceWSK("DATA_INDICATION[%d][%d]=%04x: length %Iu, skipped, offset %Iu",
				  index, j, ptr4log(i), buf.Length, offset);
			offset -= buf.Length;
			continue;
		}

		const ULONG priority = NormalPagePriority | MdlMappingNoWrite | MdlMappingNoExecute;

		auto sysaddr = (char*)MmGetSystemAddressForMdlSafe(buf.Mdl, priority);
		if (!sysaddr) {
			Trace(TRACE_LEVEL_ERROR, "DATA_INDICATION[%d][%d]=%04x: MmGetSystemAddressForMdlSafe error", 
				                  index, j, ptr4log(i));
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		sysaddr += buf.Offset + offset;
		auto remaining = buf.Length - offset;

		auto cnt = min(len, remaining);

		RtlCopyMemory(dest, sysaddr, cnt);
		dest = static_cast<char*>(dest) + cnt;
		len -= cnt;

		TraceWSK("DATA_INDICATION[%d][%d]=%04x: length %Iu, %Iu bytes copied from offset %Iu, left to copy %Iu", 
			  index, j, ptr4log(i), buf.Length, cnt, offset, len);

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
		TraceWSK("DATA_INDICATION[%d]=%04x, size %Iu", cnt, ptr4log(DataIndication), BytesIndicated);
		vpdo.wsk_data[cnt++] = DataIndication;
	}

	return ok;
}

bool wsk_data_pop(_Inout_ vpdo_dev_t &vpdo, _In_ bool release_last)
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

	if (!(cnt || release_last)) {
		TraceWSK("DATA_INDICATION %04x dropped", ptr4log(victim));
	} else if (auto err = release(vpdo.sock, victim)) {
		Trace(TRACE_LEVEL_ERROR, "DATA_INDICATION %04x release %!STATUS!", ptr4log(victim), err);
	} else {
		TraceWSK("DATA_INDICATION %04x released", ptr4log(victim));
	}

	return true;
}

void wsk_data_consume(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len)
{
	while (len && vpdo.wsk_data_cnt) {

		auto di = *vpdo.wsk_data;
		auto di_len = wsk::size(di);
		auto remaining = di_len - vpdo.wsk_data_offset;

		if (remaining > len) {
			TraceWSK("DATA_INDICATION %04x, size %Iu, %Iu bytes consumed from offset %Iu, %Iu bytes remaining", 
				  ptr4log(di), di_len, len, vpdo.wsk_data_offset, remaining - len);

			vpdo.wsk_data_offset += len;
			break;
		}

		len -= remaining;

		TraceWSK("DATA_INDICATION %04x, size %Iu, %Iu bytes consumed from offset %Iu, %Iu bytes left to consume",
			  ptr4log(di), di_len, remaining, vpdo.wsk_data_offset, len);

		NT_VERIFY(wsk_data_pop(vpdo, false)); // do not release last (just pushed) element 
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
