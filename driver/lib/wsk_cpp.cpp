/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntddk.h>
#include "wsk_cpp.h"
#include <wdm.h>
#include <ntstrsafe.h>

/*
* @see https://github.com/wbenny/KSOCKET
*/ 

namespace
{

const ULONG WSK_POOL_TAG = 'AKSW';

const WSK_CLIENT_DISPATCH g_WskDispatch{ MAKE_WSK_VERSION(1, 0) };
WSK_REGISTRATION g_WskRegistration;

enum { F_REGISTER, F_CAPTURE };
LONG g_init_flags;


class SOCKET_ASYNC_CONTEXT
{
public:
        SOCKET_ASYNC_CONTEXT() { ctor(); } // works for allocations on stack only
        ~SOCKET_ASYNC_CONTEXT() { dtor(); }

        NTSTATUS ctor(); // use if an object is allocated on heap, f.e. by ExAllocatePool2
        void dtor();

        SOCKET_ASYNC_CONTEXT(const SOCKET_ASYNC_CONTEXT&) = delete;
        SOCKET_ASYNC_CONTEXT& operator=(const SOCKET_ASYNC_CONTEXT&) = delete;

        explicit operator bool() const { return m_irp; }
        auto operator !() const { return !m_irp; }

        auto irp() const { NT_ASSERT(*this); return m_irp; }

        NTSTATUS wait_for_completion(_Inout_ NTSTATUS &status);
        void reset();

private:
        IRP *m_irp{};
        KEVENT m_completion_event;

        static NTSTATUS completion(_In_ PDEVICE_OBJECT, _In_ PIRP, _In_reads_opt_(_Inexpressible_("varies")) PVOID Context);

        void set_completetion_routine()
        {
                IoSetCompletionRoutine(m_irp, completion, &m_completion_event, true, true, true);
        }
};

NTSTATUS SOCKET_ASYNC_CONTEXT::ctor()
{
        NT_ASSERT(!*this);

        m_irp = IoAllocateIrp(1, false);
        if (!m_irp) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        KeInitializeEvent(&m_completion_event, SynchronizationEvent, false);
        set_completetion_routine();

        return STATUS_SUCCESS;
}

void SOCKET_ASYNC_CONTEXT::dtor()
{
        if (auto ptr = (IRP*)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&m_irp), nullptr)) {
                IoFreeIrp(ptr);
        }
}

void SOCKET_ASYNC_CONTEXT::reset()
{
        NT_ASSERT(*this);
        KeResetEvent(&m_completion_event);
        IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
        set_completetion_routine();
}

NTSTATUS SOCKET_ASYNC_CONTEXT::completion(
        _In_ PDEVICE_OBJECT, _In_ PIRP, _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
        KeSetEvent(static_cast<KEVENT*>(Context), IO_NO_INCREMENT, false);
        return StopCompletion;
}

NTSTATUS SOCKET_ASYNC_CONTEXT::wait_for_completion(_Inout_ NTSTATUS &status)
{
        NT_ASSERT(*this);

        if (status == STATUS_PENDING) {
                KeWaitForSingleObject(&m_completion_event, Executive, KernelMode, false, nullptr);
                status = m_irp->IoStatus.Status;
        }

        return status;
}

_Use_decl_annotations_
ULONG NTAPI ProviderNpiInit(_Inout_ PRTL_RUN_ONCE, _Inout_opt_ PVOID Parameter, [[maybe_unused]] _Inout_opt_ PVOID* Context)
{
        NT_ASSERT(!Context);
        auto prov = static_cast<WSK_PROVIDER_NPI*>(Parameter);

        bool ok = prov && !WskCaptureProviderNPI(&g_WskRegistration, WSK_INFINITE_WAIT, prov);
        if (ok) {
                InterlockedBitTestAndSet(&g_init_flags, F_CAPTURE);
        }

        return ok;
}

WSK_PROVIDER_NPI *GetProviderNPIOnce(bool testonly = false)
{
        static RTL_RUN_ONCE once = RTL_RUN_ONCE_INIT;
        static WSK_PROVIDER_NPI prov;

        if (RtlRunOnceExecuteOnce(&once, ProviderNpiInit, testonly ? nullptr : &prov, nullptr)) {
                NT_ASSERT(!prov.Client);
        }

        return prov.Client ? &prov : nullptr;
}

void ReleaseProviderNPIOnce()
{
        if (GetProviderNPIOnce(true) && InterlockedBitTestAndReset(&g_init_flags, F_CAPTURE)) {
                WskReleaseProviderNPI(&g_WskRegistration);
        }
}

void deinitialize()
{
        if (InterlockedBitTestAndReset(&g_init_flags, F_REGISTER)) {
                WskDeregister(&g_WskRegistration);
        }
}

} // namespace


struct wsk::SOCKET
{
        WSK_SOCKET *Self;

        union { // shortcuts to Self->Dispatch
                const WSK_PROVIDER_BASIC_DISPATCH *Basic;
                const WSK_PROVIDER_LISTEN_DISPATCH *Listen;
                const WSK_PROVIDER_DATAGRAM_DISPATCH *Datagram;
                const WSK_PROVIDER_CONNECTION_DISPATCH *Connection;
                const WSK_PROVIDER_STREAM_DISPATCH *Stream;
        };

        SOCKET_ASYNC_CONTEXT ctx;
};


namespace
{

NTSTATUS alloc_socket(_Out_ wsk::SOCKET* &sock)
{
        sock = (wsk::SOCKET*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*sock), WSK_POOL_TAG);
        if (!sock) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = sock->ctx.ctor();
        if (err) {
                ExFreePoolWithTag(sock, WSK_POOL_TAG);
                sock = nullptr;
        }

        return err;
}

void free(_In_ wsk::SOCKET *sock)
{
        if (sock) {
                sock->ctx.dtor();
                ExFreePoolWithTag(sock, WSK_POOL_TAG);
        }
}

auto setsockopt(_In_ wsk::SOCKET *sock, int level, int optname, void *optval, size_t optsize)
{
        return control(sock, WskSetOption, optname, level, optsize, optval, 0, nullptr, nullptr, true, nullptr);
}

auto getsockopt(_In_ wsk::SOCKET *sock, int level, int optname, void *optval, size_t optsize)
{
        SIZE_T actual = 0;
        if (auto err = control(sock, WskGetOption, optname, level, 0, nullptr, optsize, optval, nullptr, true, &actual)) {
                return err;
        }

        return actual == optsize ?  STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;

}

auto transfer(_In_ wsk::SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, SIZE_T &actual, bool send)
{
        auto &ctx = sock->ctx;
        ctx.reset();

        auto f = send ? sock->Connection->WskSend : sock->Connection->WskReceive; 

        auto err = f(sock->Self, buffer, flags, ctx.irp());
        ctx.wait_for_completion(err);

        actual = err ? 0 : ctx.irp()->IoStatus.Information;
        return err;
}

} // namespace


NTSTATUS wsk::send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _In_ IRP *irp)
{
        return sock->Connection->WskSend(sock->Self, buffer, flags, irp);
}

NTSTATUS wsk::send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags)
{
        SIZE_T actual = 0;
        auto err = transfer(sock, buffer, flags, actual, true);
        NT_ASSERT(err || actual == buffer->Length);
        return err;
}

NTSTATUS wsk::receive(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _Out_opt_ SIZE_T *actual)
{
        if (!actual) {
                flags |= WSK_FLAG_WAITALL;
        }

        SIZE_T received = 0;
        auto err = transfer(sock, buffer, flags, received, false);

        if (actual) {
                *actual = received;
        } else {
                NT_ASSERT(err || received == buffer->Length);
        }

        return err;
}

NTSTATUS wsk::getaddrinfo(
        _Out_ ADDRINFOEXW* &Result,
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints)
{
        Result = nullptr;

        auto prov = GetProviderNPIOnce();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        SOCKET_ASYNC_CONTEXT ctx;
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

void wsk::free(_In_ ADDRINFOEXW *AddrInfo)
{
        if (AddrInfo) {
                auto prov = GetProviderNPIOnce();
                NT_ASSERT(prov);
                prov->Dispatch->WskFreeAddressInfo(prov->Client, AddrInfo);
        }
}

NTSTATUS wsk::socket(
        _Out_ SOCKET* &Result,
        _In_ ADDRESS_FAMILY AddressFamily,
        _In_ USHORT SocketType,
        _In_ ULONG Protocol,
        _In_ ULONG Flags,
        _In_opt_ void *SocketContext, 
        _In_opt_ const void *Dispatch)
{
        auto &sock = Result;
        sock = nullptr;

        auto prov = GetProviderNPIOnce();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        if (auto err = alloc_socket(sock)) {
                return err;
        }

        auto irp = sock->ctx.irp();

        auto err = prov->Dispatch->WskSocket(prov->Client, AddressFamily, SocketType, Protocol, Flags, SocketContext, Dispatch,
                                                nullptr, // OwningProcess
                                                nullptr, // OwningThread
                                                nullptr, // SecurityDescriptor
                                                irp);

        sock->ctx.wait_for_completion(err);
                
        if (!err) {
                sock->Self = (WSK_SOCKET*)irp->IoStatus.Information;
                sock->Basic = static_cast<decltype(sock->Basic)>(sock->Self->Dispatch);
        } else {
                ::free(sock);
                sock = nullptr;
        }

        return err;
}

NTSTATUS wsk::control_client(
        _In_ ULONG ControlCode,
        _In_ SIZE_T InputSize,
        _In_reads_bytes_opt_(InputSize) void *InputBuffer,
        _In_ SIZE_T OutputSize,
        _Out_writes_bytes_opt_(OutputSize) void *OutputBuffer,
        _Out_opt_ SIZE_T *OutputSizeReturned,
        _In_ bool use_irp)
{
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

        SOCKET_ASYNC_CONTEXT ctx;
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto err = WskControlClient(prov->Client, ControlCode, InputSize, InputBuffer,
                OutputSize, OutputBuffer, OutputSizeReturned, ctx.irp());

        if (!ctx.wait_for_completion(err) && OutputSizeReturned) {
                *OutputSizeReturned = ctx.irp()->IoStatus.Information;
        }

        return err;
}

NTSTATUS wsk::control(
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
        auto &ctx = sock->ctx;
        if (use_irp) {
                ctx.reset();
        }

        auto err = sock->Basic->WskControlSocket(sock->Self, RequestType, ControlCode, Level, 
                                                 InputSize, InputBuffer, 
                                                 OutputSize, OutputBuffer, OutputSizeReturned, 
                                                 use_irp ? ctx.irp() : nullptr);

        if (use_irp) {
                ctx.wait_for_completion(err);
                if (OutputSizeReturnedIrp) {
                        *OutputSizeReturnedIrp = ctx.irp()->IoStatus.Information;
                }
        }

        return err;
}

NTSTATUS wsk::event_callback_control(_In_ SOCKET *sock, ULONG EventMask, bool wait4disable)
{
        if (wait4disable && !(EventMask & WSK_EVENT_DISABLE)) {
                return STATUS_INVALID_PARAMETER;
        }

        WSK_EVENT_CALLBACK_CONTROL r{ &NPI_WSK_INTERFACE_ID, EventMask };

        return control(sock, WskSetOption, SO_WSK_EVENT_CALLBACK, SOL_SOCKET, 
                       sizeof(r), &r, 0, nullptr, nullptr, wait4disable, nullptr);
}

NTSTATUS wsk::resume_receive_event(_In_ SOCKET *sock)
{
        WSK_BUF buf{};
        return receive(sock, &buf); 
}

NTSTATUS wsk::close(_In_ SOCKET *sock)
{
        if (!sock) {
                return STATUS_INVALID_PARAMETER;
        }

        sock->ctx.reset();

        auto err = sock->Basic->WskCloseSocket(sock->Self, sock->ctx.irp());
        sock->ctx.wait_for_completion(err);

        ::free(sock);
        return err;
}

NTSTATUS wsk::bind(_In_ SOCKET *sock, _In_ SOCKADDR *LocalAddress)
{
        auto &ctx = sock->ctx;
        ctx.reset();

        auto err = sock->Connection->WskBind(sock->Self, LocalAddress, 0, ctx.irp());
        return ctx.wait_for_completion(err);
}

NTSTATUS wsk::connect(_In_ SOCKET *sock, _In_ SOCKADDR *RemoteAddress)
{
        auto &ctx = sock->ctx;
        ctx.reset();

        auto err = sock->Connection->WskConnect(sock->Self, RemoteAddress, 0, ctx.irp());
        return ctx.wait_for_completion(err);
}

NTSTATUS wsk::disconnect(_In_ SOCKET *sock, _In_opt_ WSK_BUF *buffer, _In_ ULONG flags)
{
        auto &ctx = sock->ctx;
        ctx.reset();

        auto err = sock->Connection->WskDisconnect(sock->Self, buffer, flags, ctx.irp());
        return ctx.wait_for_completion(err);
}

NTSTATUS wsk::release(_In_ SOCKET *sock, _In_ WSK_DATA_INDICATION *DataIndication)
{
        return sock->Connection->WskRelease(sock->Self, DataIndication);
}

NTSTATUS wsk::getlocaladdr(_In_ SOCKET *sock, _Out_ SOCKADDR *LocalAddress)
{
        auto &ctx = sock->ctx;
        ctx.reset();

        auto err = sock->Connection->WskGetLocalAddress(sock->Self, LocalAddress, ctx.irp());
        return ctx.wait_for_completion(err);
}

NTSTATUS wsk::getremoteaddr(_In_ SOCKET *sock, _Out_ SOCKADDR *RemoteAddress)
{
        auto &ctx = sock->ctx;
        ctx.reset();

        auto err = sock->Connection->WskGetRemoteAddress(sock->Self, RemoteAddress, ctx.irp());
        return ctx.wait_for_completion(err);
}

auto wsk::for_each(
        _In_ ULONG Flags, _In_opt_ void *SocketContext, _In_opt_ const void *Dispatch,
        _In_ const ADDRINFOEXW *head, _In_ addrinfo_f f, _In_opt_ void *ctx) -> SOCKET*
{
        for (auto ai = head; ai; ai = ai->ai_next) {

                SOCKET *sock{};

                if (socket(sock, static_cast<ADDRESS_FAMILY>(ai->ai_family), static_cast<USHORT>(ai->ai_socktype), 
                           ai->ai_protocol, Flags, SocketContext, Dispatch)) {
                        // continue;
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
NTSTATUS wsk::get_keepalive(_In_ SOCKET *sock, bool &optval)
{
        return getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}

/*
 * TCP_NODELAY is not supported, see WSK_FLAG_NODELAY.
 * TCP_KEEPIDLE default is 2*60*60 sec.
 */
NTSTATUS wsk::set_keepalive(_In_ SOCKET *sock, int idle, int cnt, int intvl)
{
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

NTSTATUS wsk::get_keepalive_opts(_In_ SOCKET *sock, int *idle, int *cnt, int *intvl)
{
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

NTSTATUS wsk::initialize()
{
        WSK_CLIENT_NPI npi{ nullptr, &g_WskDispatch };

        auto err = WskRegister(&npi, &g_WskRegistration);
        if (!err) {
                InterlockedBitTestAndSet(&g_init_flags, F_REGISTER);
        }

        return err;
}

void wsk::shutdown()
{
        ReleaseProviderNPIOnce();
        deinitialize();
}

WSK_PROVIDER_NPI* wsk::GetProviderNPI()
{
        return GetProviderNPIOnce();
}

const char* wsk::ReceiveEventFlags(char *buf, size_t len, ULONG Flags)
{
        auto st = RtlStringCbPrintfA(buf, len, "%s%s%s",
                                        Flags & WSK_FLAG_RELEASE_ASAP ? ":RELEASE_ASAP" : "",
                                        Flags & WSK_FLAG_ENTIRE_MESSAGE ? ":ENTIRE_MESSAGE" : "",
                                        Flags & WSK_FLAG_AT_DISPATCH_LEVEL ? ":AT_DISPATCH_LEVEL" : "");

        return st != STATUS_INVALID_PARAMETER ? buf : "ReceiveEventFlags invalid parameter";
}

size_t wsk::size(_In_ const WSK_DATA_INDICATION *di)
{
        size_t total = 0;

        for (auto i = di; i; i = i->Next) {
                total += i->Buffer.Length;
        }

        return total;
}

WSK_DATA_INDICATION* wsk::tail(_In_ WSK_DATA_INDICATION *head)
{
        auto i = head;
        for ( ; i && i->Next; i = i->Next);
        return i;
}

