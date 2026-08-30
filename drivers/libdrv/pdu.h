/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <kernelspecs.h>

namespace usbip
{

struct header;
struct iso_packet_descriptor;

enum class swap_dir { host2net, net2host };

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void byteswap_header(_Inout_ header &hdr, _In_ swap_dir dir);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void byteswap_payload(_Inout_ header &hdr);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void byteswap(_Inout_updates_(cnt) iso_packet_descriptor *d, _In_ size_t cnt);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool get_payload_size(_Out_ size_t &result, _In_ const header &hdr);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool get_total_size(_Out_ size_t &result, _In_ const header &hdr);

} // namespace usbip
