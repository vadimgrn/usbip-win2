/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wsk.h>

namespace wsk
{

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS initialize();

_IRQL_requires_(PASSIVE_LEVEL)
void shutdown(); 

_IRQL_requires_(PASSIVE_LEVEL)
WSK_PROVIDER_NPI *GetProviderNPI();

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS control_client(
        _In_ ULONG ControlCode,
        _In_ SIZE_T InputSize,
        _In_reads_bytes_opt_(InputSize) void *InputBuffer,
        _In_ SIZE_T OutputSize,
        _Out_writes_bytes_opt_(OutputSize) void *OutputBuffer,
        _Out_opt_ SIZE_T *OutputSizeReturned,
        _In_ bool use_irp);

struct SOCKET;

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS socket(
        _Outptr_ SOCKET* &sock,
        _In_ ADDRESS_FAMILY AddressFamily,
        _In_ USHORT SocketType,
        _In_ ULONG Protocol,
        _In_ ULONG Flags,
        _In_opt_ void *SocketContext, 
        _In_opt_ const void *Dispatch);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS control(
        _In_ SOCKET *sock,
        _In_ WSK_CONTROL_SOCKET_TYPE RequestType,
        _In_ ULONG ControlCode,
        _In_ ULONG Level,
        _In_ SIZE_T InputSize,
        _In_reads_bytes_opt_(InputSize) PVOID InputBuffer,
        _In_ SIZE_T OutputSize,
        _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
        _Out_opt_ SIZE_T *OutputSizeReturned,
        _In_ bool use_irp,
        _Out_opt_ SIZE_T *OutputSizeReturnedIrp);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS event_callback_control(_In_ SOCKET *sock, ULONG EventMask, bool wait4disable);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS bind(_In_ SOCKET *sock, _In_ SOCKADDR *LocalAddress);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS connect(_In_ SOCKET *sock, _In_ SOCKADDR *RemoteAddress);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS getlocaladdr(_In_ SOCKET *sock, _Out_ SOCKADDR *LocalAddress);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS getremoteaddr(_In_ SOCKET *sock, _Out_ SOCKADDR *RemoteAddress);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags = 0);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS receive(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags = 0, _Out_opt_ SIZE_T *actual = nullptr);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _In_ IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS release(_In_ SOCKET *sock, _In_ WSK_DATA_INDICATION *DataIndication);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS disconnect(_In_ SOCKET *sock, _In_opt_ WSK_BUF *buffer = nullptr, _In_ ULONG flags = 0);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS close(_In_ SOCKET *sock);

//

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS get_keepalive(_In_ SOCKET *sock, bool &optval);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS get_keepalive_opts(_In_ SOCKET *sock, int *idle, int *cnt, int *intvl);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS set_keepalive(_In_ SOCKET *sock, int idle = 0, int cnt = 0, int intvl = 0);

//

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS getaddrinfo(
        _Outptr_ ADDRINFOEXW* &Result,
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints);

_IRQL_requires_max_(APC_LEVEL)
void free(_In_opt_ ADDRINFOEXW *AddrInfo);

//

using addrinfo_f = NTSTATUS (_In_ SOCKET *sock, _In_ const ADDRINFOEXW &ai, _Inout_opt_ void *ctx);

_IRQL_requires_max_(APC_LEVEL)
SOCKET *for_each(
        _In_ ULONG Flags, _In_opt_ void *SocketContext, _In_opt_ const void *Dispatch, // for FN_WSK_SOCKET
        _In_ const ADDRINFOEXW *head, _In_ addrinfo_f f, _Inout_opt_ void *ctx);

enum { RECEIVE_EVENT_FLAGS_BUFBZ = 64 };

_IRQL_requires_max_(DISPATCH_LEVEL)
const char *ReceiveEventFlags(_Out_ char *buf, _In_ size_t len, _In_ ULONG Flags);

_IRQL_requires_max_(DISPATCH_LEVEL)
WSK_DATA_INDICATION *tail(_In_opt_ WSK_DATA_INDICATION *di);

_IRQL_requires_max_(DISPATCH_LEVEL)
size_t size(_In_opt_ const WSK_DATA_INDICATION *di);

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto size(_In_ const WSK_DATA_INDICATION &di) { return size(&di); }

} // namespace wsk
