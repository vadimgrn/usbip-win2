/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntddk.h>
#include "wsk_cpp.h"

#include <ntstrsafe.h>

namespace
{

const ULONG WSK_POOL_TAG = 'KSWV';

const WSK_CLIENT_DISPATCH g_WskDispatch{ MAKE_WSK_VERSION(1, 0) };
WSK_REGISTRATION g_WskRegistration;

enum { F_REGISTER, F_CAPTURE };
LONG g_init_flags;


class socket_async_context
{
public:
        socket_async_context() { ctor(); } // works for allocations on stack only
        ~socket_async_context() { dtor(); }

        _IRQL_requires_max_(DISPATCH_LEVEL)
        NTSTATUS ctor(); // use if an object is allocated on heap, f.e. by ExAllocatePool2

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void dtor();

        socket_async_context(const socket_async_context &) = delete;
        socket_async_context& operator=(const socket_async_context&) = delete;

        explicit operator bool() const { return m_irp; }
        auto operator !() const { return !m_irp; }

        auto irp() const { NT_ASSERT(*this); return m_irp; }

        _IRQL_requires_max_(APC_LEVEL)
        PAGED NTSTATUS wait_for_completion(_Inout_ NTSTATUS &status);

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void reset();

private:
        IRP *m_irp{};
        KEVENT m_completion_event;

        _IRQL_requires_max_(DISPATCH_LEVEL)
        static NTSTATUS completion(_In_ PDEVICE_OBJECT, _In_ PIRP, _In_ PVOID Context);

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void set_completetion_routine()
        {
                IoSetCompletionRoutine(m_irp, completion, &m_completion_event, true, true, true);
        }
};

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS socket_async_context::ctor()
{
        NT_ASSERT(!*this);

        m_irp = IoAllocateIrp(1, false); // will be allocated from lookaside list
        if (!m_irp) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        KeInitializeEvent(&m_completion_event, SynchronizationEvent, false);
        set_completetion_routine();

        return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void socket_async_context::dtor()
{
        if (auto ptr = (IRP*)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&m_irp), nullptr)) {
                IoFreeIrp(ptr);
        }
}

/*
 * KeClearEvent(&m_completion_event);
 * SynchronizationEvent is also called an autoreset or autoclearing event.
 * The kernel automatically resets the event to the not-signaled state each time a wait is satisfied.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
void socket_async_context::reset()
{
        NT_ASSERT(*this);
        IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
        set_completetion_routine();
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS socket_async_context::completion(_In_ PDEVICE_OBJECT, _In_ PIRP irp, _In_ PVOID Context)
{
        if (irp->PendingReturned) {
                KeSetEvent(static_cast<KEVENT*>(Context), IO_NO_INCREMENT, false);
        }

        return StopCompletion;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS socket_async_context::wait_for_completion(_Inout_ NTSTATUS &status)
{
        PAGED_CODE();
        NT_ASSERT(*this);

        if (status == STATUS_PENDING) {
                KeWaitForSingleObject(&m_completion_event, Executive, KernelMode, false, nullptr);
                status = m_irp->IoStatus.Status;
        }

        return status;
}

_Function_class_(RTL_RUN_ONCE_INIT_FN)
_When_(Parameter, _IRQL_requires_(PASSIVE_LEVEL))
_When_(!Parameter, _IRQL_requires_max_(DISPATCH_LEVEL))
_IRQL_requires_same_
PAGED ULONG NTAPI ProviderNpiInit(
        _Inout_ PRTL_RUN_ONCE, _Inout_opt_ PVOID Parameter, [[maybe_unused]] _Inout_opt_ PVOID* Context)
{
        PAGED_CODE();
        NT_ASSERT(!Context);

        auto prov = static_cast<WSK_PROVIDER_NPI*>(Parameter);

        bool ok = prov && !WskCaptureProviderNPI(&g_WskRegistration, WSK_INFINITE_WAIT, prov);
        if (ok) {
                InterlockedBitTestAndSet(&g_init_flags, F_CAPTURE);
        }

        return ok;
}

_When_(!testonly, _IRQL_requires_(PASSIVE_LEVEL))
_When_(testonly, _IRQL_requires_max_(APC_LEVEL))
PAGED auto GetProviderNPIOnce(bool testonly = false)
{
        PAGED_CODE();

        static RTL_RUN_ONCE once = RTL_RUN_ONCE_INIT;
        static WSK_PROVIDER_NPI prov;

        if (RtlRunOnceExecuteOnce(&once, ProviderNpiInit, testonly ? nullptr : &prov, nullptr)) {
                NT_ASSERT(!prov.Client);
        }

        return prov.Client ? &prov : nullptr;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED void ReleaseProviderNPIOnce()
{
        PAGED_CODE();

        if (GetProviderNPIOnce(true) && InterlockedBitTestAndReset(&g_init_flags, F_CAPTURE)) {
                WskReleaseProviderNPI(&g_WskRegistration);
        }
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void deinitialize()
{
        PAGED_CODE();

        if (InterlockedBitTestAndReset(&g_init_flags, F_REGISTER)) {
                WskDeregister(&g_WskRegistration);
        }
}

} // namespace


struct wsk::SOCKET
{
        WSK_SOCKET *Self;
        socket_async_context close_ctx;

        union { // shortcuts to Self->Dispatch
                const WSK_PROVIDER_BASIC_DISPATCH *Basic;
                const WSK_PROVIDER_LISTEN_DISPATCH *Listen;
                const WSK_PROVIDER_DATAGRAM_DISPATCH *Datagram;
                const WSK_PROVIDER_CONNECTION_DISPATCH *Connection;
                const WSK_PROVIDER_STREAM_DISPATCH *Stream;
        };
};


namespace
{

_IRQL_requires_max_(DISPATCH_LEVEL)
auto alloc_socket(_Out_ wsk::SOCKET* &sock)
{
	sock = (wsk::SOCKET*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(wsk::SOCKET), WSK_POOL_TAG);
	if (!sock) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

        auto err = sock->close_ctx.ctor();
        if (err) {
                ExFreePoolWithTag(sock, WSK_POOL_TAG);
                sock = nullptr;
        }

	return err;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void free(_In_ wsk::SOCKET *sock)
{
        if (sock) {
                sock->close_ctx.dtor();
                ExFreePoolWithTag(sock, WSK_POOL_TAG);
        }
}

PAGED auto setsockopt(_In_ wsk::SOCKET *sock, int level, int optname, void *optval, size_t optsize)
{
        PAGED_CODE();
        return control(sock, WskSetOption, optname, level, optsize, optval, 0, nullptr, nullptr, true, nullptr);
}

PAGED auto getsockopt(_In_ wsk::SOCKET *sock, int level, int optname, void *optval, size_t optsize)
{
        PAGED_CODE();

        SIZE_T actual = 0;
        if (auto err = control(sock, WskGetOption, optname, level, 0, nullptr, optsize, optval, nullptr, true, &actual)) {
                return err;
        }

        return actual == optsize ?  STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;

}

_IRQL_requires_max_(APC_LEVEL)
PAGED auto transfer(_In_ wsk::SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, SIZE_T &actual, _In_ bool send)
{
        PAGED_CODE();
        NT_ASSERT(sock);

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto f = send ? sock->Connection->WskSend : sock->Connection->WskReceive;

        auto err = f(sock->Self, buffer, flags, ctx.irp());
        ctx.wait_for_completion(err);

        NT_ASSERT(err != STATUS_NOT_SUPPORTED);
        actual = NT_SUCCESS(err) ? ctx.irp()->IoStatus.Information : 0;

        return err;
}

} // namespace


_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS wsk::send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _In_ IRP *irp)
{
        NT_ASSERT(sock);
        return sock->Connection->WskSend(sock->Self, buffer, flags, irp);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS wsk::receive(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _In_ IRP *irp)
{
        NT_ASSERT(sock);
        return sock->Connection->WskReceive(sock->Self, buffer, flags, irp);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags)
{
        PAGED_CODE();

        SIZE_T actual = 0;
        auto err = transfer(sock, buffer, flags, actual, true);

        if (NT_SUCCESS(err) && actual != buffer->Length) {
                err = STATUS_PARTIAL_COPY;
        }

        return err;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::receive(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _Out_opt_ SIZE_T *actual)
{
        PAGED_CODE();

        if (!actual) {
                flags |= WSK_FLAG_WAITALL;
        }

        SIZE_T received = 0;
        auto err = transfer(sock, buffer, flags, received, false);

        if (actual) {
                *actual = received;
        } else if (NT_SUCCESS(err) && received != buffer->Length) {
                err = STATUS_PARTIAL_COPY;
        }

        return err;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::getaddrinfo(
        _Out_ ADDRINFOEXW* &Result,
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints)
{
        PAGED_CODE();
        Result = nullptr;

        auto prov = GetProviderNPIOnce();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = prov->Dispatch->WskGetAddressInfo(prov->Client, NodeName, ServiceName,
                                                        0, // NameSpace
                                                        nullptr, // Provider
                                                        Hints,
                                                        &Result,
                                                        nullptr, // OwningProcess
                                                        nullptr, // OwningThread
                                                        ctx.irp());

        return ctx.wait_for_completion(err);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED void wsk::free(_In_opt_ ADDRINFOEXW *AddrInfo)
{
        PAGED_CODE();

        if (AddrInfo) {
                auto prov = GetProviderNPIOnce();
                NT_ASSERT(prov);
                prov->Dispatch->WskFreeAddressInfo(prov->Client, AddrInfo);
        }
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::socket(
        _Out_ SOCKET* &sock,
        _In_ ADDRESS_FAMILY AddressFamily,
        _In_ USHORT SocketType,
        _In_ ULONG Protocol,
        _In_ ULONG Flags,
        _In_opt_ void *SocketContext,
        _In_opt_ const void *Dispatch)
{
        PAGED_CODE();
        sock = nullptr;

        auto prov = GetProviderNPIOnce();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        if (auto err = alloc_socket(sock)) {
                return err;
        }

        auto &ctx = sock->close_ctx;

        auto err = prov->Dispatch->WskSocket(prov->Client, AddressFamily, SocketType, Protocol, Flags, SocketContext, Dispatch,
                                                nullptr, // OwningProcess
                                                nullptr, // OwningThread
                                                nullptr, // SecurityDescriptor
                                                ctx.irp());

        ctx.wait_for_completion(err);

        if (!err) {
                sock->Self = (WSK_SOCKET*)ctx.irp()->IoStatus.Information;
                sock->Basic = static_cast<decltype(sock->Basic)>(sock->Self->Dispatch);
        } else {
                ::free(sock);
                sock = nullptr;
        }

        return err;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::control_client(
        _In_ ULONG ControlCode,
        _In_ SIZE_T InputSize,
        _In_reads_bytes_opt_(InputSize) void *InputBuffer,
        _In_ SIZE_T OutputSize,
        _Out_writes_bytes_opt_(OutputSize) void *OutputBuffer,
        _Out_opt_ SIZE_T *OutputSizeReturned,
        _In_ bool use_irp)
{
        PAGED_CODE();

        if (use_irp && OutputBuffer && !OutputSizeReturned) {
                return STATUS_INVALID_PARAMETER;
        }

        auto prov = GetProviderNPIOnce();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        auto WskControlClient = prov->Dispatch->WskControlClient;

        if (!use_irp) {
                return WskControlClient(prov->Client, ControlCode, InputSize, InputBuffer,
                                        OutputSize, OutputBuffer, OutputSizeReturned, nullptr);
        }

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = WskControlClient(prov->Client, ControlCode, InputSize, InputBuffer,
                                    OutputSize, OutputBuffer, OutputSizeReturned, ctx.irp());

        ctx.wait_for_completion(err);

        if (NT_SUCCESS(err) && OutputSizeReturned) {
                *OutputSizeReturned = ctx.irp()->IoStatus.Information;
        }

        return err;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::control(
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
        _Out_opt_ SIZE_T *OutputSizeReturnedIrp)
{
        PAGED_CODE();

        if (OutputSizeReturnedIrp) {
                *OutputSizeReturnedIrp = 0;
        }

        socket_async_context ctx;
        if (!ctx && use_irp) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = sock->Basic->WskControlSocket(sock->Self, RequestType, ControlCode, Level,
                                                 InputSize, InputBuffer,
                                                 OutputSize, OutputBuffer, OutputSizeReturned,
                                                 use_irp ? ctx.irp() : nullptr);

        if (use_irp) {
                ctx.wait_for_completion(err);
                if (NT_SUCCESS(err) && OutputSizeReturnedIrp) {
                        *OutputSizeReturnedIrp = ctx.irp()->IoStatus.Information;
                }
        }

        return err;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::event_callback_control(_In_ SOCKET *sock, ULONG EventMask, bool wait4disable)
{
        PAGED_CODE();

        if (wait4disable && !(EventMask & WSK_EVENT_DISABLE)) {
                return STATUS_INVALID_PARAMETER;
        }

        WSK_EVENT_CALLBACK_CONTROL r{ &NPI_WSK_INTERFACE_ID, EventMask };

        return control(sock, WskSetOption, SO_WSK_EVENT_CALLBACK, SOL_SOCKET,
                       sizeof(r), &r, 0, nullptr, nullptr, wait4disable, nullptr);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::close(_In_ SOCKET *sock)
{
        PAGED_CODE();

        if (!sock) {
                return STATUS_INVALID_PARAMETER;
        }

        auto &ctx = sock->close_ctx;
        ctx.reset();

        auto err = sock->Basic->WskCloseSocket(sock->Self, ctx.irp());
        ctx.wait_for_completion(err);

        ::free(sock);
        return err;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::bind(_In_ SOCKET *sock, _In_ SOCKADDR *LocalAddress)
{
        PAGED_CODE();

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = sock->Connection->WskBind(sock->Self, LocalAddress, 0, ctx.irp());
        return ctx.wait_for_completion(err);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::connect(_In_ SOCKET *sock, _In_ SOCKADDR *RemoteAddress)
{
        PAGED_CODE();

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = sock->Connection->WskConnect(sock->Self, RemoteAddress, 0, ctx.irp());
        return ctx.wait_for_completion(err);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::disconnect(_In_ SOCKET *sock, _In_opt_ WSK_BUF *buffer, _In_ ULONG flags)
{
        PAGED_CODE();

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = sock->Connection->WskDisconnect(sock->Self, buffer, flags, ctx.irp());
        return ctx.wait_for_completion(err);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS wsk::release(_In_ SOCKET *sock, _In_ WSK_DATA_INDICATION *DataIndication)
{
        return sock->Connection->WskRelease(sock->Self, DataIndication);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::getlocaladdr(_In_ SOCKET *sock, _Out_ SOCKADDR *LocalAddress)
{
        PAGED_CODE();

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = sock->Connection->WskGetLocalAddress(sock->Self, LocalAddress, ctx.irp());
        return ctx.wait_for_completion(err);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::getremoteaddr(_In_ SOCKET *sock, _Out_ SOCKADDR *RemoteAddress)
{
        PAGED_CODE();

        socket_async_context ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = sock->Connection->WskGetRemoteAddress(sock->Self, RemoteAddress, ctx.irp());
        return ctx.wait_for_completion(err);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED auto wsk::for_each(
        _In_ ULONG Flags, _In_opt_ void *SocketContext, _In_opt_ const void *Dispatch,
        _In_ const ADDRINFOEXW *head, _In_ addrinfo_f f, _Inout_opt_ void *ctx) -> SOCKET*
{
        PAGED_CODE();

        for (auto ai = head; ai; ai = ai->ai_next) {

                SOCKET *sock{};

                if (socket(sock, static_cast<ADDRESS_FAMILY>(ai->ai_family), static_cast<USHORT>(ai->ai_socktype),
                           ai->ai_protocol, Flags, SocketContext, Dispatch)) {
                        NT_ASSERT(!sock);
                } else if (auto err = f(sock, *ai, ctx)) {
                        err = close(sock);
                        NT_ASSERT(!err);
                } else {
                        return sock;
                }
        }

        return nullptr;
}

/*
 * Error if optval is ULONG, one byte is written actually.
 */
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::get_keepalive(_In_ SOCKET *sock, bool &optval)
{
        PAGED_CODE();
        return getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}

/*
 * TCP_NODELAY is not supported, see WSK_FLAG_NODELAY.
 * TCP_KEEPIDLE default is 2*60*60 sec.
 */
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::set_keepalive(_In_ SOCKET *sock, int idle, int cnt, int intvl)
{
        PAGED_CODE();

        bool optval = true; // ULONG is OK too
        if (auto err = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval))) {
                return err;
        }

        static_assert(sizeof(idle) == sizeof(ULONG));

        if (idle > 0) {
                if (auto err = setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle))) { // seconds
                        return err;
                }
        }

        if (cnt > 0) {
                if (auto err = setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt))) {
                        return err;
                }
        }

        if (intvl > 0) {
                if (auto err = setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl))) { // seconds
                        return err;
                }
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::get_keepalive_opts(_In_ SOCKET *sock, int *idle, int *cnt, int *intvl)
{
        PAGED_CODE();

        if (idle) {
                if (auto err = getsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, idle, sizeof(*idle))) {
                        return err;
                }
        }

        if (cnt) {
                if (auto err = getsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, cnt, sizeof(*cnt))) {
                        return err;
                }
        }

        if (intvl) {
                if (auto err = getsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, intvl, sizeof(*intvl))) {
                        return err;
                }
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS wsk::initialize()
{
        PAGED_CODE();
        WSK_CLIENT_NPI npi{ nullptr, &g_WskDispatch };

        auto err = WskRegister(&npi, &g_WskRegistration);
        if (!err) {
                InterlockedBitTestAndSet(&g_init_flags, F_REGISTER);
        }

        return err;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void wsk::shutdown()
{
        PAGED_CODE();
        ReleaseProviderNPIOnce();
        deinitialize();
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGED WSK_PROVIDER_NPI* wsk::GetProviderNPI()
{
        PAGED_CODE();
        return GetProviderNPIOnce();
}

_IRQL_requires_max_(DISPATCH_LEVEL)
const char* wsk::ReceiveEventFlags(_Out_ char *buf, _In_ size_t len, _In_ ULONG Flags)
{
        auto st = RtlStringCbPrintfA(buf, len, "%s%s%s",
                                        Flags & WSK_FLAG_RELEASE_ASAP ? ":RELEASE_ASAP" : "",
                                        Flags & WSK_FLAG_ENTIRE_MESSAGE ? ":ENTIRE_MESSAGE" : "",
                                        Flags & WSK_FLAG_AT_DISPATCH_LEVEL ? ":AT_DISPATCH_LEVEL" : "");

        return st != STATUS_INVALID_PARAMETER ? buf : "ReceiveEventFlags invalid parameter";
}

_IRQL_requires_max_(DISPATCH_LEVEL)
size_t wsk::size(_In_opt_ const WSK_DATA_INDICATION *di)
{
        size_t total = 0;

        for ( ; di; di = di->Next) {
                total += di->Buffer.Length;
        }

        return total;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
WSK_DATA_INDICATION* wsk::tail(_In_opt_ WSK_DATA_INDICATION *di)
{
        for ( ; di && di->Next; di = di->Next);
        return di;
}
