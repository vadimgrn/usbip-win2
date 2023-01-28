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
#include <libdrv\dbgcommon.h>
#include <libdrv\strutil.h>

#include <ntstrsafe.h>
#include <usbuser.h>

namespace
{

using namespace usbip;

static_assert(sizeof(vhci::ioctl_plugin_hardware::service) == NI_MAXSERV);
static_assert(sizeof(vhci::ioctl_plugin_hardware::host) == NI_MAXHOST);

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
PAGED void fill(_Out_ vhci::ioctl_get_imported_devices &dst, _In_ const device_ctx &ctx)
{
        PAGED_CODE();
        auto &src = *ctx.ext;

//      ioctl_plugin_hardware

        dst.out.port = ctx.port;
        dst.out.error = 0;

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
                }
        }

        static_cast<vhci::imported_dev_data&>(dst) = src.dev;
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

        PACK_OP_COMMON(0, &req.hdr);
        PACK_OP_IMPORT_REQUEST(0, &req.body);

        return send(ext.sock, memory::stack, &req, sizeof(req));
}

/*
 * @return err_t or op_status_t 
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED int recv_rep_import(_In_ device_ctx_ext &ext, _In_ memory pool, _Out_ op_import_reply &reply)
{
        PAGED_CODE();

        if (auto err = recv_op_common(ext.sock, OP_REP_IMPORT)) {
                return err;
        }

        if (auto err = recv(ext.sock, pool, &reply, sizeof(reply))) {
                Trace(TRACE_LEVEL_ERROR, "Receive op_import_reply %!STATUS!", err);
                return ERR_NETWORK;
        }
        PACK_OP_IMPORT_REPLY(false, &reply);

        if (strncmp(reply.udev.busid, ext.busid, sizeof(reply.udev.busid))) {
                Trace(TRACE_LEVEL_ERROR, "Received busid(%s) != expected(%s)", reply.udev.busid, ext.busid);
                return ERR_PROTOCOL;
        }

        return ERR_NONE;
}

/*
 * @return err_t or op_status_t 
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED int import_remote_device(_Inout_ device_ctx_ext &ext)
{
        PAGED_CODE();

        if (auto err = send_req_import(ext)) {
                Trace(TRACE_LEVEL_ERROR, "Send OP_REQ_IMPORT %!STATUS!", err);
                return ERR_NETWORK;
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

        return ERR_NONE;
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
                return ERR_NETWORK;
        }

        NT_ASSERT(!ext.sock);
        ext.sock = wsk::for_each(WSK_FLAG_CONNECTION_SOCKET, &ext, nullptr, ai, try_connect, nullptr);

        wsk::free(ai);
        return ext.sock ? ERR_NONE : ERR_NETWORK;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugin(_Out_ int &port, _In_ UDECXUSBDEVICE dev)
{
        PAGED_CODE();

        port = vhci::claim_roothub_port(dev);
        if (!port) {
                Trace(TRACE_LEVEL_ERROR, "All roothub ports are occupied");
                return ERR_PORTFULL;
        }

        auto speed = get_device_ctx(dev)->speed();

        UDECX_USB_DEVICE_PLUG_IN_OPTIONS options; 
        UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);

        auto &portnum = speed < USB_SPEED_SUPER ? options.Usb20PortNumber : options.Usb30PortNumber;
        portnum = port;

        if (auto err = UdecxUsbDevicePlugIn(dev, &options)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugIn %!STATUS!", err);
                return ERR_GENERAL;
        }

        return ERR_NONE;
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

        return ERR_NONE;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugin_hardware(_In_ WDFDEVICE vhci, _Inout_ vhci::ioctl_plugin_hardware &r)
{
        PAGED_CODE();
        Trace(TRACE_LEVEL_INFORMATION, "%s:%s, busid %s", r.host, r.service, r.busid);

        auto &port = r.out.port;
        port = 0;

        auto &error = r.out.error;

        device_ctx_ext_ptr ext;
        if (NT_ERROR(create_device_ctx_ext(ext.ptr, r))) {
                error = ERR_GENERAL;
                return;
        }

        if (bool(error = connect(*ext.ptr))) {
                Trace(TRACE_LEVEL_ERROR, "Can't connect to %!USTR!:%!USTR!", &ext->node_name, &ext->service_name);
                return;
        }

        Trace(TRACE_LEVEL_INFORMATION, "Connected to %!USTR!:%!USTR!", &ext->node_name, &ext->service_name);

        if (bool(error = import_remote_device(*ext.ptr))) {
                return;
        }

        UDECXUSBDEVICE dev;
        if (NT_ERROR(device::create(dev, vhci, ext.ptr))) {
                error = ERR_GENERAL;
                return;
        }
        ext.release(); // now dev owns it

        if (bool(error = start_device(port, dev))) {
                WdfObjectDelete(dev); // UdecxUsbDevicePlugIn failed or was not called
        } else {
                Trace(TRACE_LEVEL_INFORMATION, "dev %04x -> port %d", ptr04x(dev), port);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugin_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugin_hardware *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), reinterpret_cast<PVOID*>(&r), nullptr)) {
                return err;
        }

        if (auto vhci = get_vhci(Request)) {
                plugin_hardware(vhci, *r);
        }

        WdfRequestSetInformation(Request, sizeof(r->out));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugout_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugout_hardware *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), reinterpret_cast<PVOID*>(&r), nullptr)) {
                return err;
        }

        auto vhci = get_vhci(Request);
        auto err = STATUS_SUCCESS;

        if (r->port <= 0) {
                vhci::destroy_all_devices(vhci);
        } else if (auto dev = vhci::find_device(vhci, r->port)) {
                device::plugout_and_delete(dev.get<UDECXUSBDEVICE>());
        } else {
                Trace(TRACE_LEVEL_ERROR, "Invalid or empty port %d", r->port);
                err = STATUS_NO_SUCH_DEVICE;
        }

        return err;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_imported_devices(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        size_t buf_sz;
        vhci::ioctl_get_imported_devices *buf;
        if (auto err = WdfRequestRetrieveOutputBuffer(Request, sizeof(*buf), reinterpret_cast<PVOID*>(&buf), &buf_sz)) {
                return err;
        }

        auto vhci = get_vhci(Request);
        int idx = 0;

        for (int port = 1; port <= ARRAYSIZE(vhci_ctx::devices) && idx < buf_sz/sizeof(*buf); ++port) {
                if (auto dev = vhci::find_device(vhci, port)) {
                        auto &ctx = *get_device_ctx(dev.get());
                        fill(buf[idx++], ctx);
                }
        }

        TraceDbg("%d device(s) reported", idx);

        WdfRequestSetInformation(Request, idx*sizeof(*buf));
        return STATUS_SUCCESS;
}

/*
 * IRP_MJ_DEVICE_CONTROL
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
        case vhci::ioctl::plugin_hardware:
                st = plugin_hardware(Request);
                break;
        case vhci::ioctl::plugout_hardware:
                st = plugout_hardware(Request);
                break;
        case vhci::ioctl::get_imported_devices:
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
