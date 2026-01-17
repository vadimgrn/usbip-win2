/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntddk.h>
#include "wsk_cpp.h"
#include "wait_timeout.h"

#include <ntstrsafe.h>

namespace
{

const ULONG WSK_POOL_TAG = 'KSWV';

const WSK_CLIENT_DISPATCH g_Dispatch{ MAKE_WSK_VERSION(1, 0) };
WSK_REGISTRATION g_Registration;

enum { F_REGISTER, F_CAPTURE };
LONG g_init_flags;


#if DBG

class ConcurrencyCheck
{
public:
#ifdef _WIN64
        ConcurrencyCheck(_In_opt_ LONG64 *cnt) : m_cnt(cnt) {}
#else
        ConcurrencyCheck(_In_opt_ LONG *cnt) : m_cnt(cnt) {}
#endif

        ~ConcurrencyCheck()
        {
#ifdef _WIN64
                NT_ASSERT(!m_cnt || InterlockedIncrement64(m_cnt) == m_val + 1); // there were no concurrent calls
#else
                NT_ASSERT(!m_cnt || InterlockedIncrement(m_cnt) == m_val + 1); // there were no concurrent calls
#endif
        }

        ConcurrencyCheck(const ConcurrencyCheck&) = delete;
        ConcurrencyCheck& operator =(const ConcurrencyCheck&) = delete;

private:
#ifdef _WIN64
        LONG64 *m_cnt{};
        LONG64 m_val = m_cnt ? InterlockedIncrement64(m_cnt) : 0;
#else
        LONG *m_cnt{};
        LONG m_val = m_cnt ? InterlockedIncrement(m_cnt) : 0;
#endif
};

#else

class ConcurrencyCheck
{
public:
#ifdef _WIN64
        ConcurrencyCheck(_In_opt_ LONG64*) {}
#else
        ConcurrencyCheck(_In_opt_ LONG*) {}
#endif
};

#endif // if DBG


class irp_cls
{
public:
        irp_cls() { ctor(); } // works for allocations on stack only
        ~irp_cls() { dtor(); }

        _IRQL_requires_max_(DISPATCH_LEVEL) // use if an object is allocated on heap, f.e. by ExAllocatePool2
        NTSTATUS ctor();

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void dtor();

        irp_cls(_In_ const irp_cls &) = delete;
        irp_cls& operator=(_In_ const irp_cls&) = delete;

        explicit operator bool() const { return m_irp; }
        auto operator !() const { return !m_irp; }

        auto get() const { return m_irp; }
        auto operator ->() const { return m_irp; }

        _IRQL_requires_max_(APC_LEVEL)
        PAGED NTSTATUS wait_for_completion(_Inout_ NTSTATUS &status);

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void reset();

private:
        IRP *m_irp{};
        KEVENT m_event{};

        _IRQL_requires_max_(DISPATCH_LEVEL)
        static NTSTATUS completion(_In_ DEVICE_OBJECT*, _In_ IRP*, _In_ void *context);

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void set_completetion_routine()
        {
                IoSetCompletionRoutine(m_irp, completion, this, true, true, true);
        }
};

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS irp_cls::ctor()
{
        NT_ASSERT(!*this);

        m_irp = IoAllocateIrp(1, false); // will be allocated from lookaside list
        if (!m_irp) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        KeInitializeEvent(&m_event, SynchronizationEvent, false);
        set_completetion_routine();

        return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
inline void irp_cls::dtor()
{
        if (auto ptr = (IRP*)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&m_irp), nullptr)) {
                IoFreeIrp(ptr);
        }
}

/*
 * SynchronizationEvent is also called an autoreset or autoclearing event.
 * The kernel automatically resets the event to the not-signaled state each time a wait is satisfied.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
void irp_cls::reset()
{
        NT_ASSERT(*this);
        IoReuseIrp(m_irp, STATUS_SUCCESS);
        set_completetion_routine();
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS irp_cls::completion(_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_ void *context)
{
        auto &self = *static_cast<irp_cls*>(context);

        if (irp->PendingReturned) {
                KeSetEvent(&self.m_event, IO_NO_INCREMENT, false);
        }

        return StopCompletion;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS irp_cls::wait_for_completion(_Inout_ NTSTATUS &status)
{
        PAGED_CODE();
        NT_ASSERT(*this);

        if (status == STATUS_PENDING) {
                NT_VERIFY(!KeWaitForSingleObject(&m_event, Executive, KernelMode, false, nullptr));
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
        enum { TIMEOUT = 5*60*1000 }; // milliseconds, @see WSK_INFINITE_WAIT

        auto ok = prov && NT_SUCCESS(WskCaptureProviderNPI(&g_Registration, TIMEOUT, prov));
        if (ok) {
                InterlockedBitTestAndSet(&g_init_flags, F_CAPTURE);
        }

        return ok;
}

/*
 * Must not be called after ReleaseProviderNPI.
 */
_When_(!testonly, _IRQL_requires_(PASSIVE_LEVEL))
_When_(testonly, _IRQL_requires_max_(APC_LEVEL))
PAGED auto GetProviderNPI(bool testonly = false)
{
        PAGED_CODE();

        static RTL_RUN_ONCE once = RTL_RUN_ONCE_INIT;
        static WSK_PROVIDER_NPI prov;

        RtlRunOnceExecuteOnce(&once, ProviderNpiInit, testonly ? nullptr : &prov, nullptr);
        return BitTest(&g_init_flags, F_CAPTURE) ? &prov : nullptr;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED void ReleaseProviderNPI()
{
        PAGED_CODE();

        if (GetProviderNPI(true); InterlockedBitTestAndReset(&g_init_flags, F_CAPTURE)) {
                WskReleaseProviderNPI(&g_Registration);
        }
}

} // namespace


struct wsk::SOCKET
{
        WSK_SOCKET *Self;

        irp_cls recv_irp; // recv/send can be called concurrently
        irp_cls send_irp;
        irp_cls misc_irp;

        union { // shortcuts to Self->Dispatch
                const WSK_PROVIDER_BASIC_DISPATCH *Basic;
                const WSK_PROVIDER_LISTEN_DISPATCH *Listen;
                const WSK_PROVIDER_DATAGRAM_DISPATCH *Datagram;
                const WSK_PROVIDER_CONNECTION_DISPATCH *Connection;
                const WSK_PROVIDER_STREAM_DISPATCH *Stream;
        };

#ifdef _WIN64
        using count_t = LONG64;
#else
        using count_t = LONG;
#endif

        count_t recv_cnt;
        count_t sent_cnt;
        count_t misc_cnt;

        enum : count_t { // three highest bits are flags, lower bits comprise a counter
#ifdef _WIN64
                SIGN = count_t(1) << 63,
                EVENT_SET_OFFSET = 62, EVENT_SET = count_t(1) << EVENT_SET_OFFSET,
                CLOSING = count_t(1) << 61,
#else
                SIGN = count_t(1) << 31,
                EVENT_SET_OFFSET = 30, EVENT_SET = count_t(1) << EVENT_SET_OFFSET,
                CLOSING = count_t(1) << 29,
#endif
                COUNT_MASK = ~(SIGN | EVENT_SET | CLOSING)
        };

        count_t invoke_cnt;
        KEVENT can_close;

        template<typename F, typename... Args>
        auto invoke(count_t *cnt, F &&f, Args&&... args) 
        {
                NTSTATUS ret;

#ifdef _WIN64
                if (auto n = InterlockedIncrement64(&invoke_cnt); n & CLOSING) {
#else
                if (auto n = InterlockedIncrement(&invoke_cnt); n & CLOSING) {
#endif
                        ret = STATUS_NOT_SUPPORTED; // WSK callbacks do not complete IRP if this status is returned
                } else {
                        ConcurrencyCheck chk(cnt);
                        ret = f(args...);

                        using R = decltype(f(args...)); // @see std::invoke_result
                        static_assert(sizeof(R) == sizeof(ret)); // R must be NTSTATUS
                }

#ifdef _WIN64
                if (InterlockedDecrement64(&invoke_cnt) == CLOSING && // count is zero, event is not set
                    !InterlockedBitTestAndSet64(&invoke_cnt, EVENT_SET_OFFSET)) {
#else
                if (InterlockedDecrement(&invoke_cnt) == CLOSING && // count is zero, event is not set
                    !InterlockedBitTestAndSet(&invoke_cnt, EVENT_SET_OFFSET)) {
#endif
                        NT_VERIFY(!KeSetEvent(&can_close, IO_NO_INCREMENT, false)); // once
                }

                return ret;
        }
};


namespace
{

using namespace wsk;

_IRQL_requires_max_(DISPATCH_LEVEL)
auto alloc_socket(_Out_ SOCKET* &sock)
{
        sock = (SOCKET*)ExAllocatePoolZero(NonPagedPoolNx, sizeof(SOCKET), WSK_POOL_TAG);
        if (!sock) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        irp_cls* v[] = {
                &sock->recv_irp,
                &sock->send_irp,
                &sock->misc_irp,
        };

        for (auto irp: v) {
                if (auto err = irp->ctor()) {
                        free(sock);
                        return err;
                }
        }

        KeInitializeEvent(&sock->can_close, NotificationEvent, false);
        return STATUS_SUCCESS;
}

PAGED auto setsockopt(_In_ SOCKET *sock, int level, int optname, void *optval, size_t optsize)
{
        PAGED_CODE();
        return control(sock, WskSetOption, optname, level, optsize, optval, 0, nullptr, nullptr, true, nullptr);
}

PAGED auto getsockopt(_In_ SOCKET *sock, int level, int optname, void *optval, size_t optsize)
{
        PAGED_CODE();

        SIZE_T actual = 0;
        if (auto err = control(sock, WskGetOption, optname, level, 0, nullptr, optsize, optval, nullptr, true, &actual)) {
                return err;
        }

        return actual == optsize ?  STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;

}

_IRQL_requires_max_(APC_LEVEL)
PAGED auto transfer(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _Out_ SIZE_T &actual, _In_ bool send)
{
        PAGED_CODE();
        NT_ASSERT(sock);

        irp_cls *irp;
        SOCKET::count_t *cnt;
        PFN_WSK_SEND func;

        if (auto con = sock->Connection; send) {
                irp = &sock->send_irp;
                cnt = &sock->sent_cnt;
                func = con->WskSend;
        } else {
                irp = &sock->recv_irp;
                cnt = &sock->recv_cnt;
                func = con->WskReceive;
        }

        irp->reset();

        auto st = sock->invoke(cnt, func, sock->Self, buffer, flags, irp->get());
        irp->wait_for_completion(st);

        actual = NT_SUCCESS(st) ? (*irp)->IoStatus.Information : 0;
        return st;
}

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED auto wait_invokers(_Inout_ SOCKET &s)
{
        PAGED_CODE();

#ifdef _WIN64
        if (auto n = InterlockedOr64(&s.invoke_cnt, s.CLOSING); n & s.CLOSING) {
#else
        if (auto n = InterlockedOr(&s.invoke_cnt, s.CLOSING); n & s.CLOSING) {
#endif
                return STATUS_NOT_SUPPORTED; // must be called once
        } else if (n) { // count is not zero
                NT_ASSERT((n & s.COUNT_MASK) == n);
                auto timeout = make_timeout(30*wdm::second, wdm::period::relative);
                NT_VERIFY(!KeWaitForSingleObject(&s.can_close, Executive, KernelMode, false, &timeout));
        } else {
#ifdef _WIN64
                InterlockedBitTestAndSet64(&s.invoke_cnt, s.EVENT_SET_OFFSET); // do not set event, it's all over
#else
                InterlockedBitTestAndSet(&s.invoke_cnt, s.EVENT_SET_OFFSET); // do not set event, it's all over
#endif
        }

        NT_ASSERT(!(s.invoke_cnt & s.COUNT_MASK));
        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS wsk::send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _In_ IRP *irp)
{
        NT_ASSERT(sock);
        return sock->invoke(&sock->sent_cnt, sock->Connection->WskSend, sock->Self, buffer, flags, irp);
}

/*
 * FIXME: 
 * recv_cnt is commented out because WSK thread has high priority and completion handler is called
 * before WSK function returns control.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS wsk::receive(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _In_ IRP *irp)
{
        NT_ASSERT(sock);
        return sock->invoke(nullptr /*&sock->recv_cnt*/, sock->Connection->WskReceive, sock->Self, buffer, flags, irp);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::send(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags)
{
        PAGED_CODE();

        SIZE_T sent = 0;
        auto st = transfer(sock, buffer, flags, sent, true);

        if (NT_SUCCESS(st) && sent != buffer->Length) {
                st = STATUS_PARTIAL_COPY;
        }

        return st;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::receive(_In_ SOCKET *sock, _In_ WSK_BUF *buffer, _In_ ULONG flags, _Out_opt_ SIZE_T *actual)
{
        PAGED_CODE();

        if (!actual) {
                flags |= WSK_FLAG_WAITALL;
        }

        SIZE_T received = 0;
        auto st = transfer(sock, buffer, flags, received, false);

        if (actual) {
                *actual = received;
        } else if (NT_SUCCESS(st) && received != buffer->Length) {
                st = STATUS_RECEIVE_PARTIAL;
        }

        return st;
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::getaddrinfo(
        _Out_ ADDRINFOEXW* &Result,
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints,
        _Inout_ IRP *irp)
{
        PAGED_CODE();
        Result = nullptr;

        auto prov = GetProviderNPI();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        return prov->Dispatch->WskGetAddressInfo(prov->Client, NodeName, ServiceName,
                                                0, // NameSpace
                                                nullptr, // Provider
                                                Hints,
                                                &Result,
                                                nullptr, // OwningProcess
                                                nullptr, // OwningThread
                                                irp);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED void wsk::free(_In_opt_ ADDRINFOEXW *AddrInfo)
{
        PAGED_CODE();

        if (AddrInfo) {
                auto prov = GetProviderNPI();
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

        auto prov = GetProviderNPI();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        if (auto err = alloc_socket(sock)) {
                return err;
        }

        auto &irp = sock->misc_irp;

        auto st = prov->Dispatch->WskSocket(prov->Client, AddressFamily, SocketType, Protocol, Flags, SocketContext, Dispatch,
                                                nullptr, // OwningProcess
                                                nullptr, // OwningThread
                                                nullptr, // SecurityDescriptor
                                                irp.get());

        irp.wait_for_completion(st);

        if (NT_SUCCESS(st)) {
                sock->Self = reinterpret_cast<WSK_SOCKET*>(irp->IoStatus.Information);
                sock->Basic = static_cast<decltype(sock->Basic)>(sock->Self->Dispatch);
        } else {
                free(sock);
        }

        return st;
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

        auto prov = GetProviderNPI();
        if (!prov) {
                return STATUS_UNSUCCESSFUL;
        }

        auto WskControlClient = prov->Dispatch->WskControlClient;

        if (!use_irp) {
                return WskControlClient(prov->Client, ControlCode, InputSize, InputBuffer,
                                        OutputSize, OutputBuffer, OutputSizeReturned, nullptr);
        }

        irp_cls irp;
        if (!irp) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto st = WskControlClient(prov->Client, ControlCode, InputSize, InputBuffer,
                                    OutputSize, OutputBuffer, OutputSizeReturned, irp.get());

        irp.wait_for_completion(st);

        if (NT_SUCCESS(st) && OutputSizeReturned) {
                *OutputSizeReturned = irp->IoStatus.Information;
        }

        return st;
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

        auto &irp = sock->misc_irp;
        if (use_irp) {
                irp.reset();
        }

        auto st = sock->invoke(&sock->misc_cnt,
                                sock->Basic->WskControlSocket, 
                                sock->Self, RequestType, ControlCode, Level,
                                InputSize, InputBuffer,
                                OutputSize, OutputBuffer, OutputSizeReturned,
                                use_irp ? irp.get() : nullptr);

        if (use_irp) {
                irp.wait_for_completion(st);
                if (NT_SUCCESS(st) && OutputSizeReturnedIrp) {
                        *OutputSizeReturnedIrp = irp->IoStatus.Information;
                }
        }

        return st;
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

/*
 * Before calling the WskCloseSocket function, a WSK application must ensure that there are no other 
 * function calls in progress to any of the socket's functions, including any extension functions, 
 * in any of the application's other threads.
 * 
 * After this call, further calls must return STATUS_NOT_SUPPORTED.
 * @see SOCKET::invoke
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::close(_In_ SOCKET *sock)
{
        PAGED_CODE();

        if (!sock) {
                return STATUS_INVALID_PARAMETER;
        }

        if (auto err = wait_invokers(*sock)) {
                return err;
        }

        auto &irp = sock->misc_irp;
        irp.reset();

        auto st = sock->Basic->WskCloseSocket(sock->Self, irp.get());
        irp.wait_for_completion(st);

        return st;
}

/*
 * @param sock must be closed 
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
void wsk::free(_Inout_ SOCKET* &sock)
{
        if (auto sk = (SOCKET*)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&sock), nullptr)) {

                sk->recv_irp.dtor();
                sk->send_irp.dtor();
                sk->misc_irp.dtor();

                ExFreePoolWithTag(sk, WSK_POOL_TAG);
        }
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::bind(_In_ SOCKET *sock, _In_ SOCKADDR *LocalAddress)
{
        PAGED_CODE();

        auto &irp = sock->misc_irp;
        irp.reset();

        auto st = sock->invoke(&sock->misc_cnt, sock->Connection->WskBind, sock->Self, LocalAddress, 0, irp.get());
        return irp.wait_for_completion(st);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS wsk::connect(_In_ SOCKET *sock, _In_ SOCKADDR *RemoteAddress, _In_ IRP *irp)
{
        return sock->invoke(nullptr, sock->Connection->WskConnect, sock->Self, RemoteAddress, 0, irp);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::disconnect(_In_ SOCKET *sock, _In_opt_ WSK_BUF *buffer, _In_ ULONG flags)
{
        PAGED_CODE();

        auto &irp = sock->misc_irp;
        irp.reset();

        auto st = sock->invoke(&sock->misc_cnt, sock->Connection->WskDisconnect, sock->Self, buffer, flags, irp.get());
        return irp.wait_for_completion(st);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::getlocaladdr(_In_ SOCKET *sock, _Out_ SOCKADDR *LocalAddress)
{
        PAGED_CODE();

        auto &irp = sock->misc_irp;
        irp.reset();

        auto st = sock->invoke(&sock->misc_cnt, sock->Connection->WskGetLocalAddress, sock->Self, LocalAddress, irp.get());
        return irp.wait_for_completion(st);
}

_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS wsk::getremoteaddr(_In_ SOCKET *sock, _Out_ SOCKADDR *RemoteAddress)
{
        PAGED_CODE();

        auto &irp = sock->misc_irp;
        irp.reset();

        auto st = sock->invoke(&sock->misc_cnt, sock->Connection->WskGetRemoteAddress, sock->Self, RemoteAddress, irp.get());
        return irp.wait_for_completion(st);
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
        WSK_CLIENT_NPI npi{ .Dispatch = &g_Dispatch };

        auto st = WskRegister(&npi, &g_Registration);
        if (NT_SUCCESS(st)) {
                InterlockedBitTestAndSet(&g_init_flags, F_REGISTER);
        }

        return st;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void wsk::shutdown()
{
        PAGED_CODE();
        ReleaseProviderNPI();

        if (InterlockedBitTestAndReset(&g_init_flags, F_REGISTER)) {
                WskDeregister(&g_Registration);
        }
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
