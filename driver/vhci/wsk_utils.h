#pragma once

#include <wdm.h>
#include <wsk.h>

namespace usbip
{

struct SOCKET;

ADDRINFOEXW *getaddrinfo(
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints,
        _Out_opt_ NTSTATUS *Result = nullptr);

void free(_In_ ADDRINFOEXW *AddrInfo);

SOCKET *socket(
        _In_ ADDRESS_FAMILY AddressFamily,
        _In_ USHORT SocketType,
        _In_ ULONG Protocol,
        _In_ ULONG Flags,
        _Out_opt_ NTSTATUS *Result = nullptr);

NTSTATUS close(_In_ SOCKET *sock);

} // namespace usbip
