/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "vhci_ioctl.h"
#include "trace.h"
#include "vhci_ioctl.tmh"

#include "context.h"
#include "vhci.h"
#include "device.h"
#include "network.h"
#include "ioctl.h"

#include <libdrv\dbgcommon.h>
#include <libdrv\mdl_cpp.h>
#include <libdrv\strutil.h>

#include <usbip\proto_op.h>

#include <ntstrsafe.h>
#include <ws2def.h>
#include <usbuser.h>

namespace
{

using namespace usbip;

static_assert(sizeof(vhci::ioctl_plugin::service) == NI_MAXSERV);
static_assert(sizeof(vhci::ioctl_plugin::host) == NI_MAXHOST);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void log(const usbip_usb_device &d)
{
        Trace(TRACE_LEVEL_VERBOSE, "usbip_usb_device(path '%s', busid %s, busnum %d, devnum %d, %!usb_device_speed!,"
                "vid %#x, pid %#x, rev %#x, class/sub/proto %x/%x/%x, "
                "bConfigurationValue %d, bNumConfigurations %d, bNumInterfaces %d)", 
                d.path, d.busid, d.busnum, d.devnum, d.speed, 
                d.idVendor, d.idProduct, d.bcdDevice,
                d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol, 
                d.bConfigurationValue, d.bNumConfigurations, d.bNumInterfaces);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void fill(_Out_ vhci::ioctl_imported_dev &dst, _In_ const device_ctx &ctx)
{
        PAGED_CODE();
        auto &src = *ctx.ext;

//      ioctl_plugin
        dst.port = ctx.port;
        if (auto s = src.busid) {
                RtlStringCbCopyA(dst.busid, sizeof(dst.busid), s);
        }

        struct {
                char *ansi;
                USHORT ansi_sz;
                const UNICODE_STRING &ustr;
        } const v[] = {
                {dst.service, sizeof(dst.service), src.service_name},
                {dst.host, sizeof(dst.host), src.node_name},
                {dst.serial, sizeof(dst.serial), src.serial}
        };

        for (auto &[ansi, ansi_sz, ustr]: v) {
                if (auto err = to_ansi_str(ansi, ansi_sz, ustr)) {
                        Trace(TRACE_LEVEL_ERROR, "to_ansi_str('%!USTR!') %!STATUS!", &ustr, err);
                }
        }

        static_cast<vhci::ioctl_imported_dev_data&>(dst) = src.dev;
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
                op_import_request body;
        } req;

        static_assert(sizeof(req) == sizeof(req.hdr) + sizeof(req.body)); // packed

        strcpy_s(req.body.busid, sizeof(req.body.busid), ext.busid);

        PACK_OP_COMMON(0, &req.hdr);
        PACK_OP_IMPORT_REQUEST(0, &req.body);

        return send(ext.sock, memory::stack, &req, sizeof(req));
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto recv_rep_import(_In_ device_ctx_ext &ext, _In_ memory pool, _Out_ op_import_reply &reply)
{
        PAGED_CODE();

        auto status = ST_OK;

        if (auto err = recv_op_common(ext.sock, OP_REP_IMPORT, status)) {
                return make_error(err);
        }

        if (status) {
                Trace(TRACE_LEVEL_ERROR, "OP_REP_IMPORT %!op_status_t!", status);
                return make_error(ERR_NONE, status);
        }

        if (auto err = recv(ext.sock, pool, &reply, sizeof(reply))) {
                Trace(TRACE_LEVEL_ERROR, "Receive op_import_reply %!STATUS!", err);
                return make_error(ERR_NETWORK);
        }

        PACK_OP_IMPORT_REPLY(0, &reply);

        if (strncmp(reply.udev.busid, ext.busid, sizeof(reply.udev.busid))) {
                Trace(TRACE_LEVEL_ERROR, "Received busid(%s) != expected(%s)", reply.udev.busid, ext.busid);
                return make_error(ERR_PROTOCOL);
        }

        return make_error(ERR_NONE);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto import_remote_device(_Inout_ device_ctx_ext &ext)
{
        PAGED_CODE();

        if (auto err = send_req_import(ext)) {
                Trace(TRACE_LEVEL_ERROR, "Send OP_REQ_IMPORT %!STATUS!", err);
                return make_error(ERR_NETWORK);
        }

        op_import_reply reply{};

        if (auto err = recv_rep_import(ext, memory::stack, reply)) { // result made by make_error()
                return err;
        }

        auto &dev = reply.udev;
        log(dev);

        if (auto d = &ext.dev) {
                d->vendor = dev.idVendor;
                d->product = dev.idProduct;
                d->devid = make_devid(static_cast<UINT16>(dev.busnum), static_cast<UINT16>(dev.devnum));
                d->speed = static_cast<usb_device_speed>(dev.speed);
        }

        return make_error(ERR_NONE);
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
PAGED void plugin_hardware(_In_ WDFDEVICE vhci, _Inout_ vhci::ioctl_plugin &r)
{
        PAGED_CODE();
        Trace(TRACE_LEVEL_INFORMATION, "%s:%s, busid %s, serial %s", r.host, r.service, r.busid, r.serial);

        auto &error = r.port;
        error = make_error(ERR_GENERAL);

        device_ctx_ext_ptr ext;
        if (NT_ERROR(create_device_ctx_ext(ext.ptr, r))) {
                return;
        }

        if (auto err = connect(*ext.ptr)) {
                error = make_error(err);
                Trace(TRACE_LEVEL_ERROR, "Can't connect to %!USTR!:%!USTR!", &ext->node_name, &ext->service_name);
                return;
        }

        Trace(TRACE_LEVEL_INFORMATION, "Connected to %!USTR!:%!USTR!", &ext->node_name, &ext->service_name);

        if (bool(error = import_remote_device(*ext.ptr))) {
                return;
        }

        UDECXUSBDEVICE dev{};
        if (NT_ERROR(device::create(dev, vhci, ext.ptr))) {
                return;
        }
        ext.release(); // now dev owns it

        if (auto err = plugin(r.port, dev)) {
                error = make_error(err);
                WdfObjectDelete(dev); // UdecxUsbDevicePlugIn failed or was not called
        } else {
                Trace(TRACE_LEVEL_INFORMATION, "dev %04x -> port %d", ptr04x(dev), r.port);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugin_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugin *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &PVOID(r), nullptr)) {
                return err;
        }

        if (auto vhci = get_vhci(Request)) {
                plugin_hardware(vhci, *r);
        }

        WdfRequestSetInformation(Request, sizeof(r->port));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugout_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugout *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &PVOID(r), nullptr)) {
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

        size_t buf_sz = 0;
        vhci::ioctl_imported_dev *result{};
        if (auto err = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result), &PVOID(result), &buf_sz)) {
                return err;
        }

        auto cnt = buf_sz/sizeof(*result);
        int result_cnt = 0;

        auto vhci = get_vhci(Request);

        for (int port = 1; port <= ARRAYSIZE(vhci_ctx::devices) && cnt; ++port) {
                if (auto dev = vhci::find_device(vhci, port)) {
                        auto &ctx = *get_device_ctx(dev.get());
                        fill(result[result_cnt++], ctx);
                        --cnt;
                }
        }

        TraceDbg("%d device(s) reported", result_cnt);

        WdfRequestSetInformation(Request, result_cnt*sizeof(*result));
        return STATUS_SUCCESS;
}

/*
 * IRP_MJ_DEVICE_CONTROL 
 */
_Function_class_(EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void IoDeviceControl(
        _In_ WDFQUEUE Queue,
        _In_ WDFREQUEST Request,
        _In_ size_t OutputBufferLength,
        _In_ size_t InputBufferLength,
        _In_ ULONG IoControlCode)
{
        PAGED_CODE();

        TraceDbg("%s(%#08lX), OutputBufferLength %Iu, InputBufferLength %Iu", 
                  device_control_name(IoControlCode), IoControlCode, OutputBufferLength, InputBufferLength);

        USBUSER_REQUEST_HEADER *hdr{};
        auto st = STATUS_INVALID_DEVICE_REQUEST;
        auto complete = true;

        switch (IoControlCode) {
        case vhci::IOCTL_PLUGIN_HARDWARE:
                st = plugin_hardware(Request);
                break;
        case vhci::IOCTL_PLUGOUT_HARDWARE:
                st = plugout_hardware(Request);
                break;
        case vhci::IOCTL_GET_IMPORTED_DEVICES:
                st = get_imported_devices(Request);
                break;
        case IOCTL_USB_USER_REQUEST:
                NT_ASSERT(!has_urb(Request));
                if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*hdr), &PVOID(hdr), nullptr))) {
                        TraceDbg("USB_USER_REQUEST -> %s(%#08lX)", usbuser_request_name(hdr->UsbUserRequest), hdr->UsbUserRequest);
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
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchParallel);
        cfg.EvtIoDeviceControl = IoDeviceControl;
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
