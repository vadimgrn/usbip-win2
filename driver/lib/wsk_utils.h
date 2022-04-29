#pragma once

#include <wdm.h>
#include <wsk.h>

namespace wsk
{

NTSTATUS initialize();
void shutdown(); 

WSK_PROVIDER_NPI *GetProviderNPI();

struct SOCKET;

NTSTATUS socket(
        _Out_ SOCKET* &Result,
        _In_ ADDRESS_FAMILY AddressFamily,
        _In_ USHORT SocketType,
        _In_ ULONG Protocol,
        _In_ ULONG Flags,
        _In_opt_ void *SocketContext, 
        _In_opt_ const void *Dispatch);

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
        _In_ bool use_irp);

NTSTATUS bind(_In_ SOCKET *sock, _In_ SOCKADDR *LocalAddress);
NTSTATUS connect(_In_ SOCKET *sock, _In_ SOCKADDR *RemoteAddress);

NTSTATUS getlocaladdr(_In_ SOCKET *sock, _Out_ SOCKADDR *LocalAddress);
NTSTATUS getremoteaddr(_In_ SOCKET *sock, _Out_ SOCKADDR *RemoteAddress);

NTSTATUS transfer(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, SIZE_T &actual, bool send);

inline auto send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, SIZE_T &actual)
{
        return transfer(sock, buffer, flags, actual, true);
}

inline auto receive(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, SIZE_T &actual)
{
        return transfer(sock, buffer, flags, actual, false);
}

NTSTATUS disconnect(_In_ SOCKET *sock, _In_opt_ WSK_BUF *buffer = nullptr, _In_ ULONG flags = 0);
NTSTATUS release(_In_ SOCKET *sock, _In_ WSK_DATA_INDICATION *DataIndication);
NTSTATUS close(_In_ SOCKET *sock);

//

NTSTATUS get_keepalive(_In_ SOCKET *sock, bool &optval);
NTSTATUS get_keepalive_opts(_In_ SOCKET *sock, int *idle, int *cnt, int *intvl);
NTSTATUS set_keepalive(_In_ SOCKET *sock, int idle = 0, int cnt = 0, int intvl = 0);

//

NTSTATUS getaddrinfo(
        _Out_ ADDRINFOEXW* &Result,
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints);

void free(_In_ ADDRINFOEXW *AddrInfo);

//

using addrinfo_f = NTSTATUS (*)(SOCKET *sock, const ADDRINFOEXW &ai, void *ctx);

SOCKET *for_each(
        _In_ ULONG Flags, _In_opt_ void *SocketContext, _In_opt_ const void *Dispatch, // for FN_WSK_SOCKET
        _In_ const ADDRINFOEXW *head, _In_ addrinfo_f f, _In_opt_ void *ctx);

} // namespace wsk