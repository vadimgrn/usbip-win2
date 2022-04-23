#pragma once

#include <wdm.h>
#include <wsk.h>

namespace wsk
{

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
        _In_ bool async);

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

NTSTATUS setsockopt(_In_ SOCKET *sock, int level, int optname, int optval);
NTSTATUS getsockopt(_In_ SOCKET *sock, int level, int optname, int &optval);

inline auto set_nodelay(_In_ SOCKET *sock)
{
        return setsockopt(sock, SOL_SOCKET, TCP_NODELAY, 1);
}

NTSTATUS set_keepalive(_In_ SOCKET *sock, int idletime = 0, int tries_cnt = 0, int tries_intvl = 0);

inline auto get_keepalive(_In_ SOCKET *sock, int &optval)
{
        return getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, optval);
}

NTSTATUS get_keepalive_values(_In_ SOCKET *sock, int *idletime, int *tries_cnt, int *tries_intvl);

NTSTATUS getaddrinfo(
        _Out_ ADDRINFOEXW* &Result,
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints);

void free(_In_ ADDRINFOEXW *AddrInfo);


using addrinfo_f = NTSTATUS (*)(SOCKET *sock, const ADDRINFOEXW &ai, void *ctx);

SOCKET *for_each(
        _In_ ULONG Flags, _In_opt_ void *SocketContext, _In_opt_ const void *Dispatch, // for FN_WSK_SOCKET
        _In_ const ADDRINFOEXW *head, _In_ addrinfo_f f, _In_opt_ void *ctx);


class Mdl
{
public:
        Mdl(_In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length);
        ~Mdl();

        Mdl(const Mdl&) = delete;
        Mdl& operator=(const Mdl&) = delete;

        explicit operator bool() const { return m_mdl; }
        auto operator !() const { return !m_mdl; }

        auto get() const { return m_mdl; }

        auto addr() const { return m_mdl ? MmGetMdlVirtualAddress(m_mdl) : 0; }
        auto offset() const { return m_mdl ? MmGetMdlByteOffset(m_mdl) : 0; }
        auto size() const { return m_mdl ? MmGetMdlByteCount(m_mdl) : 0; }

        bool lock();
        auto locked() const { return m_mdl && (m_mdl->MdlFlags & MDL_PAGES_LOCKED); }
        void unlock();

private:
        MDL *m_mdl{};
};

} // namespace wsk
