#include "wsk_utils.h"
#include "trace.h"
#include "wsk_utils.tmh"

#include "vhci.h"

/*
* @see https://github.com/wbenny/KSOCKET
*/ 

namespace
{

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

} // namespace


struct usbip::SOCKET
{
        WSK_PROVIDER_NPI *Provider;
        WSK_SOCKET *Socket;
        SOCKET_ASYNC_CONTEXT ctx;
        union {
                const WSK_PROVIDER_BASIC_DISPATCH *BasicDispatch{};
                const WSK_PROVIDER_LISTEN_DISPATCH *ListenDispatch;
                const WSK_PROVIDER_DATAGRAM_DISPATCH *DatagramDispatch;
                const WSK_PROVIDER_CONNECTION_DISPATCH *ConnectionDispatch;
                const WSK_PROVIDER_STREAM_DISPATCH *StreamDispatch;
        };
};


namespace
{

inline auto assign(_Out_opt_ NTSTATUS *dst, _In_ NTSTATUS val = STATUS_UNSUCCESSFUL)
{
        if (dst) {
                *dst = val;
        }

        return nullptr;
}

usbip::SOCKET *new_socket(_Out_opt_ NTSTATUS *Result)
{
        auto prov = GetProviderNPI();
        if (!prov) {
                return assign(Result);
        }

        usbip::SOCKET *sock = (usbip::SOCKET*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*sock), USBIP_VHCI_POOL_TAG);
        if (!sock) {
                return assign(Result, STATUS_INSUFFICIENT_RESOURCES);
        }

        sock->Provider = prov;

        auto err = sock->ctx.ctor();
        if (err) {
                ExFreePoolWithTag(sock, USBIP_VHCI_POOL_TAG);
                sock = nullptr;
        }

        assign(Result, err);
        return sock;
}

void dtor(_In_ usbip::SOCKET *sock)
{
        if (sock) {
                sock->ctx.dtor();
                ExFreePoolWithTag(sock, USBIP_VHCI_POOL_TAG);
        }
}

} // namespace


ADDRINFOEXW *usbip::getaddrinfo(
        _In_opt_ UNICODE_STRING *NodeName,
        _In_opt_ UNICODE_STRING *ServiceName,
        _In_opt_ ADDRINFOEXW *Hints,
        _Out_opt_ NTSTATUS *Result)
{
        auto prov = GetProviderNPI();
        if (!prov) {
                return assign(Result);
        }

        SOCKET_ASYNC_CONTEXT ctx;
        if (!ctx) {
                return assign(Result, STATUS_INSUFFICIENT_RESOURCES);
        }

        ADDRINFOEXW *info{};
        auto err = prov->Dispatch->WskGetAddressInfo(prov->Client, NodeName, ServiceName,
                                                        0, // NameSpace
                                                        nullptr, // Provider
                                                        Hints, 
                                                        &info,
                                                        nullptr, // OwningProcess
                                                        nullptr, // OwningThread
                                                        ctx.irp());

        ctx.wait_for_completion(err);

        assign(Result, err);
        return info;
}

void usbip::free(_In_ ADDRINFOEXW *AddrInfo)
{
        if (AddrInfo) {
                auto prov = GetProviderNPI();
                NT_ASSERT(prov);
                prov->Dispatch->WskFreeAddressInfo(prov->Client, AddrInfo);
        }
}

auto usbip::socket(
        _In_ ADDRESS_FAMILY AddressFamily,
        _In_ USHORT SocketType,
        _In_ ULONG Protocol,
        _In_ ULONG Flags,
        _Out_opt_ NTSTATUS *Result) -> SOCKET*
{
        auto sock = new_socket(Result);
        if (!sock) {
                return nullptr;
        }

        auto WskSocket = sock->Provider->Dispatch->WskSocket;

        auto err = WskSocket(sock->Provider->Client, AddressFamily, SocketType, Protocol, Flags,
                                nullptr, // SocketContext
                                nullptr, // Dispatch
                                nullptr, // OwningProcess
                                nullptr, // OwningThread
                                nullptr, // SecurityDescriptor
                                sock->ctx.irp());

        sock->ctx.wait_for_completion(err);
                
        if (!err) {
                sock->Socket = (WSK_SOCKET*)sock->ctx.irp()->IoStatus.Information;
                sock->BasicDispatch = static_cast<const WSK_PROVIDER_BASIC_DISPATCH*>(sock->Socket->Dispatch);
        } else {
                dtor(sock);
                sock = nullptr;
        }

        assign(Result, err);
        return sock;
}

NTSTATUS usbip::close(_In_ SOCKET *sock)
{
        if (!sock) {
                return STATUS_INVALID_PARAMETER;
        }

        sock->ctx.reset();

        auto err = sock->BasicDispatch->WskCloseSocket(sock->Socket, sock->ctx.irp());
        sock->ctx.wait_for_completion(err);

        dtor(sock);
        return err;
}
