/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "vhci_ioctl.h"
#include "trace.h"
#include "vhci_ioctl.tmh"

#include "context.h"
#include "vhci.h"
#include "device.h"
#include "network.h"
#include "ioctl.h"

#include <usbip\proto_op.h>
#include <resources\messages.h>

#include <libdrv\dbgcommon.h>
#include <libdrv\strutil.h>

#include <ntstrsafe.h>
#include <usbuser.h>

namespace
{

using namespace usbip;

static_assert(sizeof(vhci::imported_device_location::service) == NI_MAXSERV);
static_assert(sizeof(vhci::imported_device_location::host) == NI_MAXHOST);

constexpr auto as_ntstatus(_In_ DWORD code)
{
        static_assert(sizeof(code) == sizeof(NTSTATUS));
        return static_cast<NTSTATUS>(code);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void log(_In_ const usbip_usb_device &d)
{
        PAGED_CODE();
        TraceDbg("usbip_usb_device(path '%s', busid %s, busnum %d, devnum %d, %!usb_device_speed!,"
                "vid %#x, pid %#x, rev %#x, class/sub/proto %x/%x/%x, "
                "bConfigurationValue %d, bNumConfigurations %d, bNumInterfaces %d)", 
                d.path, d.busid, d.busnum, d.devnum, d.speed, 
                d.idVendor, d.idProduct, d.bcdDevice,
                d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol, 
                d.bConfigurationValue, d.bNumConfigurations, d.bNumInterfaces);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto fill(_Out_ vhci::imported_device &dst, _In_ const device_ctx &ctx)
{
        PAGED_CODE();
        auto &src = *ctx.ext;

//      imported_device_location

        dst.port = ctx.port;

        if (auto s = src.busid) {
                RtlStringCbCopyA(dst.busid, sizeof(dst.busid), s);
        }

        struct {
                char *utf8;
                USHORT utf8_sz;
                const UNICODE_STRING &ustr;
        } const v[] = {
                {dst.service, sizeof(dst.service), src.service_name},
                {dst.host, sizeof(dst.host), src.node_name}
        };

        for (auto &[utf8, utf8_sz, ustr]: v) {
                if (auto err = libdrv::unicode_to_utf8(utf8, utf8_sz, ustr)) {
                        Trace(TRACE_LEVEL_ERROR, "unicode_to_utf8('%!USTR!') %!STATUS!", &ustr, err);
                        return err;
                }
        }

        static_cast<vhci::imported_device_properties&>(dst) = src.dev;
        return STATUS_SUCCESS;
}

/*
 * @see <linux>/tools/usb/usbip/src/usbipd.c, recv_request_import
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto send_req_import(_In_ device_ctx_ext &ext)
{
        PAGED_CODE();

        struct {
                op_common hdr{ USBIP_VERSION, OP_REQ_IMPORT, ST_OK };
                op_import_request body{};
        } req;

        static_assert(sizeof(req) == sizeof(req.hdr) + sizeof(req.body)); // packed

        strcpy_s(req.body.busid, sizeof(req.body.busid), ext.busid);

        PACK_OP_COMMON(false, &req.hdr);
        PACK_OP_IMPORT_REQUEST(false, &req.body);

        return send(ext.sock, memory::stack, &req, sizeof(req));
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto recv_rep_import(_In_ device_ctx_ext &ext, _In_ memory pool, _Out_ op_import_reply &reply)
{
        PAGED_CODE();
        RtlZeroMemory(&reply, sizeof(reply));

        if (auto err = recv_op_common(ext.sock, OP_REP_IMPORT)) {
                return err;
        }

        if (auto err = recv(ext.sock, pool, &reply, sizeof(reply))) {
                Trace(TRACE_LEVEL_ERROR, "Receive op_import_reply %!STATUS!", err);
                return ERROR_USBIP_NETWORK;
        }
        PACK_OP_IMPORT_REPLY(false, &reply);

        if (strncmp(reply.udev.busid, ext.busid, sizeof(reply.udev.busid))) {
                Trace(TRACE_LEVEL_ERROR, "Received bus-id '%s' != '%s'", reply.udev.busid, ext.busid);
                return ERROR_USBIP_PROTOCOL;
        }

        return 0UL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto import_remote_device(_Inout_ device_ctx_ext &ext)
{
        PAGED_CODE();

        if (auto err = send_req_import(ext)) {
                Trace(TRACE_LEVEL_ERROR, "Send OP_REQ_IMPORT %!STATUS!", err);
                return ERROR_USBIP_NETWORK;
        }

        op_import_reply reply;
        if (auto err = recv_rep_import(ext, memory::stack, reply)) {
                return err;
        }
 
        auto &udev = reply.udev; 
        log(udev);

        if (auto d = &ext.dev) {
                d->devid = make_devid(static_cast<UINT16>(udev.busnum), static_cast<UINT16>(udev.devnum));
                d->speed = static_cast<usb_device_speed>(udev.speed);
                d->vendor = udev.idVendor;
                d->product = udev.idProduct;
        }

        return 0UL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto getaddrinfo(_Out_ ADDRINFOEXW* &result, _In_ device_ctx_ext &ext)
{
        PAGED_CODE();

        ADDRINFOEXW hints{};
        hints.ai_flags = AI_NUMERICSERV;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP; // zero isn't work

        return wsk::getaddrinfo(result, &ext.node_name, &ext.service_name, &hints);
}

/*
 * TCP_NODELAY is not supported, see WSK_FLAG_NODELAY.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto set_options(wsk::SOCKET *sock)
{
        PAGED_CODE();

        auto keepalive = [] (auto idle, auto cnt, auto intvl) constexpr { return idle + cnt*intvl; };

        int idle = 0;
        int cnt = 0;
        int intvl = 0;

        if (auto err = get_keepalive_opts(sock, &idle, &cnt, &intvl)) {
                Trace(TRACE_LEVEL_ERROR, "get_keepalive_opts %!STATUS!", err);
                return err;
        }

        Trace(TRACE_LEVEL_VERBOSE, "get keepalive: idle(%d sec) + cnt(%d)*intvl(%d sec) => %d sec", 
                idle, cnt, intvl, keepalive(idle, cnt, intvl));

        enum { IDLE = 30, CNT = 9, INTVL = 10 };

        if (auto err = set_keepalive(sock, IDLE, CNT, INTVL)) {
                Trace(TRACE_LEVEL_ERROR, "set_keepalive %!STATUS!", err);
                return err;
        }

        bool optval{};
        if (auto err = get_keepalive(sock, optval)) {
                Trace(TRACE_LEVEL_ERROR, "get_keepalive %!STATUS!", err);
                return err;
        }

        NT_VERIFY(!get_keepalive_opts(sock, &idle, &cnt, &intvl));

        Trace(TRACE_LEVEL_VERBOSE, "set keepalive: idle(%d sec) + cnt(%d)*intvl(%d sec) => %d sec", 
                idle, cnt, intvl, keepalive(idle, cnt, intvl));

        bool ok = optval && keepalive(idle, cnt, intvl) == keepalive(IDLE, CNT, INTVL);
        return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto try_connect(wsk::SOCKET *sock, const ADDRINFOEXW &ai, void*)
{
        PAGED_CODE();

        if (auto err = set_options(sock)) {
                return err;
        }

        SOCKADDR_INET any{ static_cast<ADDRESS_FAMILY>(ai.ai_family) }; // see INADDR_ANY, IN6ADDR_ANY_INIT

        if (auto err = bind(sock, reinterpret_cast<SOCKADDR*>(&any))) {
                Trace(TRACE_LEVEL_ERROR, "bind %!STATUS!", err);
                return err;
        }

        auto err = connect(sock, ai.ai_addr);
        if (err) {
                Trace(TRACE_LEVEL_ERROR, "WskConnect %!STATUS!", err);
        }
        return err;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto connect(_Inout_ device_ctx_ext &ext)
{
        PAGED_CODE();

        ADDRINFOEXW *ai{};
        if (auto err = getaddrinfo(ai, ext)) {
                Trace(TRACE_LEVEL_ERROR, "getaddrinfo %!STATUS!", err);
                return ERROR_USBIP_ADDRINFO;
        }

        NT_ASSERT(!ext.sock);
        ext.sock = wsk::for_each(WSK_FLAG_CONNECTION_SOCKET, &ext, nullptr, ai, try_connect, nullptr);

        wsk::free(ai);
        return ext.sock ? 0U : ERROR_USBIP_CONNECT;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugin(_Out_ int &port, _In_ UDECXUSBDEVICE dev)
{
        PAGED_CODE();

        port = vhci::claim_roothub_port(dev);
        if (!port) {
                Trace(TRACE_LEVEL_ERROR, "All roothub ports are occupied");
                return ERROR_USBIP_PORTFULL;
        }

        auto speed = get_device_ctx(dev)->speed();

        UDECX_USB_DEVICE_PLUG_IN_OPTIONS options; 
        UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);

        auto &portnum = speed < USB_SPEED_SUPER ? options.Usb20PortNumber : options.Usb30PortNumber;
        portnum = port;

        if (auto err = UdecxUsbDevicePlugIn(dev, &options)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugIn %!STATUS!", err);
                return ERROR_USBIP_GENERAL;
        }

        return 0UL;
}

struct device_ctx_ext_ptr
{
        ~device_ctx_ext_ptr() 
        { 
                if (ptr) {
                        close_socket(ptr->sock);
                        free(ptr); 
                }
        }

        auto operator ->() const { return ptr; }
        void release() { ptr = nullptr; }

        device_ctx_ext *ptr{};
};

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto start_device(_Out_ int &port, _In_ UDECXUSBDEVICE device)
{
        PAGED_CODE();

        if (auto err = plugin(port, device)) {
                return err;
        }

        if (auto dev = get_device_ctx(device)) {
                sched_receive_usbip_header(*dev);
        }

        return 0UL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugin_hardware(_In_ WDFDEVICE vhci, _Inout_ vhci::ioctl::plugin_hardware &r)
{
        PAGED_CODE();
        Trace(TRACE_LEVEL_INFORMATION, "%s:%s, busid %s", r.host, r.service, r.busid);

        auto &port = r.port;
        r.port = 0;

        device_ctx_ext_ptr ext;
        if (NT_ERROR(create_device_ctx_ext(ext.ptr, r))) {
                return ERROR_USBIP_GENERAL;
        }

        if (auto err = connect(*ext.ptr)) {
                Trace(TRACE_LEVEL_ERROR, "Can't connect to %!USTR!:%!USTR!", &ext->node_name, &ext->service_name);
                return err;
        }

        Trace(TRACE_LEVEL_INFORMATION, "Connected to %!USTR!:%!USTR!", &ext->node_name, &ext->service_name);

        if (auto err = import_remote_device(*ext.ptr)) {
                return err;
        }

        UDECXUSBDEVICE dev;
        if (NT_ERROR(device::create(dev, vhci, ext.ptr))) {
                return ERROR_USBIP_GENERAL;
        }
        ext.release(); // now dev owns it

        if (auto err = start_device(port, dev)) {
                WdfObjectDelete(dev); // UdecxUsbDevicePlugIn failed or was not called
                return err;
        }

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x -> port %d", ptr04x(dev), port);
        return 0UL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugin_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl::plugin_hardware *r{};

        if (size_t length; 
            auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), reinterpret_cast<PVOID*>(&r), &length)) {
                return err;
        } else if (length != sizeof(*r)) {
                return STATUS_INVALID_BUFFER_SIZE;
        } else if (r->size != sizeof(*r)) {
                Trace(TRACE_LEVEL_ERROR, "plugin_hardware.size %lu != sizeof(plugin_hardware) %Iu", 
                                          r->size, sizeof(*r));

                return as_ntstatus(ERROR_USBIP_ABI);
        }

        if (auto vhci = get_vhci(Request); auto err = plugin_hardware(vhci, *r)) {
                return as_ntstatus(err);
        }

        constexpr auto written = offsetof(vhci::ioctl::plugin_hardware, port) + sizeof(r->port);
        WdfRequestSetInformation(Request, written);
        
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugout_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl::plugout_hardware *r{};

        if (size_t length; 
            auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), reinterpret_cast<PVOID*>(&r), &length)) {
                return err;
        } else if (length != sizeof(*r)) {
                return STATUS_INVALID_BUFFER_SIZE;
        } else if (r->size != sizeof(*r)) {
                Trace(TRACE_LEVEL_ERROR, "plugout_hardware.size %lu != sizeof(plugout_hardware) %Iu",
                                          r->size, sizeof(*r));

                return as_ntstatus(ERROR_USBIP_ABI);
        }

        if (auto vhci = get_vhci(Request); r->port <= 0) {
                vhci::destroy_all_devices(vhci);
        } else if (!is_valid_port(r->port)) {
                return STATUS_INVALID_PARAMETER;
        } else if (auto dev = vhci::find_device(vhci, r->port)) {
                device::plugout_and_delete(dev.get<UDECXUSBDEVICE>());
        } else {
                return STATUS_DEVICE_NOT_CONNECTED;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_imported_devices(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        size_t outlen;
        vhci::ioctl::get_imported_devices *r;

        if (auto err = WdfRequestRetrieveOutputBuffer(Request, sizeof(*r), reinterpret_cast<PVOID*>(&r), &outlen)) {
                return err;
        } else if (r->size != sizeof(*r)) {
                Trace(TRACE_LEVEL_ERROR, "get_imported_devices.size %lu != sizeof(get_imported_devices) %Iu", 
                                          r->size, sizeof(*r));

                return as_ntstatus(ERROR_USBIP_ABI);
        }

        auto devices_size = outlen - offsetof(vhci::ioctl::get_imported_devices, devices); // size of array

        auto max_cnt = devices_size/sizeof(*r->devices);
        NT_ASSERT(max_cnt);

        auto vhci = get_vhci(Request);
        ULONG cnt = 0;

        for (int port = 1; port <= ARRAYSIZE(vhci_ctx::devices); ++port) {
                if (auto dev = vhci::find_device(vhci, port)) {
                        if (cnt == max_cnt) {
                                return STATUS_BUFFER_TOO_SMALL;
                        } else if (auto ctx = get_device_ctx(dev.get()); auto err = fill(r->devices[cnt++], *ctx)) {
                                return err;
                        }
                }
        }

        TraceDbg("%lu device(s) reported", cnt);

        auto written = vhci::ioctl::get_imported_devices_size(cnt);
        NT_ASSERT(written <= outlen);
        WdfRequestSetInformation(Request, written);

        return STATUS_SUCCESS;
}

/*
 * IRP_MJ_DEVICE_CONTROL
 * 
 * This is a public driver API. How to maintain its compatibility for libusbip users.
 * 1.IOCTLs are like syscals on Linux. Once IOCTL code is released, its input/output data remain 
 *   the same for lifetime.
 * 2.If this is not possible, new IOCTL code must be added.
 * 3.IOCTL could be removed (unlike syscals) for various reasons. This will break backward compatibility.
 *   It can be declared as deprecated in some release and removed afterwards. 
 *   The removed IOCTL code must never be reused.
 */
_Function_class_(EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void device_control(
        _In_ WDFQUEUE Queue,
        _In_ WDFREQUEST Request,
        _In_ size_t OutputBufferLength,
        _In_ size_t InputBufferLength,
        _In_ ULONG IoControlCode)
{
        TraceDbg("%s(%#08lX), OutputBufferLength %Iu, InputBufferLength %Iu", 
                  device_control_name(IoControlCode), IoControlCode, OutputBufferLength, InputBufferLength);

        auto complete = true;
        auto st = STATUS_INVALID_DEVICE_REQUEST;

        switch (IoControlCode) {
        case vhci::ioctl::PLUGIN_HARDWARE:
                st = plugin_hardware(Request);
                break;
        case vhci::ioctl::PLUGOUT_HARDWARE:
                st = plugout_hardware(Request);
                break;
        case vhci::ioctl::GET_IMPORTED_DEVICES:
                st = get_imported_devices(Request);
                break;
        case IOCTL_USB_USER_REQUEST:
                NT_ASSERT(!has_urb(Request));
                if (USBUSER_REQUEST_HEADER *hdr; 
                    NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*hdr), 
                                                             reinterpret_cast<PVOID*>(&hdr), nullptr))) {
                        TraceDbg("USB_USER_REQUEST -> %s(%#08lX)", usbuser_request_name(hdr->UsbUserRequest), 
                                  hdr->UsbUserRequest);
                }
                [[fallthrough]];
        default:
                complete = !UdecxWdfDeviceTryHandleUserIoctl(WdfIoQueueGetDevice(Queue), Request); // PASSIVE_LEVEL
        }

        if (complete) {
                TraceDbg("%!STATUS!, Information %Iu", st, WdfRequestGetInformation(Request));
                WdfRequestComplete(Request, st);
        }
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::vhci::create_default_queue(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchSequential);
        cfg.EvtIoDeviceControl = device_control;
        cfg.PowerManaged = WdfFalse;

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
        attrs.EvtCleanupCallback = [] (auto) { TraceDbg("Default queue cleanup"); };
        attrs.ParentObject = vhci;

        WDFQUEUE queue;
        if (auto err = WdfIoQueueCreate(vhci, &cfg, &attrs, &queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        TraceDbg("%04x", ptr04x(queue));
        return STATUS_SUCCESS;
}
