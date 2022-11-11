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
#include "proto.h"
#include "driver.h"

#include <usbip\proto_op.h>

#include <libdrv\dbgcommon.h>
#include <libdrv\mdl_cpp.h>
#include <libdrv\strutil.h>
#include <libdrv\usbd_helper.h>
#include <libdrv\usb_util.h>
#include <libdrv\usbdsc.h>
#include <libdrv\pdu.h>
#include <libdrv\ch9.h>

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
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void log(_In_ const USB_DEVICE_DESCRIPTOR &d)
{
        PAGED_CODE();
        TraceDbg("DeviceDescriptor(bLength %d, %!usb_descriptor_type!, bcdUSB %#x, "
                "bDeviceClass %#x, bDeviceSubClass %#x, bDeviceProtocol %#x, bMaxPacketSize0 %d, "
                "idVendor %#x, idProduct %#x, bcdDevice %#x, "
                "iManufacturer %d, iProduct %d, iSerialNumber %d, "
                "bNumConfigurations %d)",
                d.bLength, d.bDescriptorType, d.bcdUSB,
                d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol, d.bMaxPacketSize0,
                d.idVendor, d.idProduct, d.bcdDevice,
                d.iManufacturer, d.iProduct, d.iSerialNumber,
                d.bNumConfigurations);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void log(_In_ const USB_CONFIGURATION_DESCRIPTOR &d)
{
        PAGED_CODE();
        TraceDbg("ConfigurationDescriptor(bLength %d, %!usb_descriptor_type!, wTotalLength %hu(%#x), "
                "bNumInterfaces %d, bConfigurationValue %d, iConfiguration %d, bmAttributes %#x, MaxPower %d)",
                d.bLength, d.bDescriptorType, d.wTotalLength, d.wTotalLength,
                d.bNumInterfaces, d.bConfigurationValue, d.iConfiguration, d.bmAttributes, d.MaxPower);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_same_device(_In_ const usbip_usb_device &dev, _In_ const USB_DEVICE_DESCRIPTOR &dsc)
{
        PAGED_CODE();
        return  dev.idVendor == dsc.idVendor &&
                dev.idProduct == dsc.idProduct &&
                dev.bcdDevice == dsc.bcdDevice &&

                dev.bDeviceClass == dsc.bDeviceClass &&
                dev.bDeviceSubClass == dsc.bDeviceSubClass &&
                dev.bDeviceProtocol == dsc.bDeviceProtocol &&

                dev.bNumConfigurations == dsc.bNumConfigurations;
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
PAGED auto init_req_get_descr(
        _Out_ usbip_header &hdr, _In_ device_ctx_ext &dev, 
        _In_ UCHAR type, _In_ UCHAR index, _In_ USHORT lang_id, _In_ USHORT TransferBufferLength)
{
        PAGED_CODE();

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

        if (auto err = set_cmd_submit_usbip_header(hdr, dev, EP0, TransferFlags, TransferBufferLength, setup_dir::in())) {
                return false;
        }

        auto &pkt = get_submit_setup(hdr);
        pkt.bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt.bRequest = USB_REQUEST_GET_DESCRIPTOR;
        pkt.wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(type, index);
        pkt.wIndex.W = lang_id; // Zero or Language ID for string descriptor
        pkt.wLength = TransferBufferLength;

        return true;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto read_descr_hdr(
        _In_ device_ctx_ext &dev, _In_ UCHAR type, _In_ UCHAR index, _In_ USHORT lang_id, 
        _Inout_ USHORT &TransferBufferLength)
{
        PAGED_CODE();

        usbip_header hdr{};
        if (!init_req_get_descr(hdr, dev, type, index, lang_id, TransferBufferLength)) {
                return ERR_GENERAL;
        }

        char buf[DBG_USBIP_HDR_BUFSZ];
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "OUT %Iu%s", get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr, true));

        byteswap_header(hdr, swap_dir::host2net);

        if (auto err = send(dev.sock, memory::stack, &hdr, sizeof(hdr))) {
                Trace(TRACE_LEVEL_ERROR, "Send header of %!usb_descriptor_type! %!STATUS!", type, err);
                return ERR_NETWORK;
        }

        if (auto err = recv(dev.sock, memory::stack, &hdr, sizeof(hdr))) {
                Trace(TRACE_LEVEL_ERROR, "Recv header of %!usb_descriptor_type! %!STATUS!", type, err);
                return ERR_NETWORK;
        }

        byteswap_header(hdr, swap_dir::net2host);
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "IN %Iu%s", get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr, true));

        auto &b = hdr.base;
        if (!(b.command == USBIP_RET_SUBMIT && extract_num(b.seqnum) == dev.seqnum)) {
                return ERR_PROTOCOL;
        }

        auto &ret = hdr.u.ret_submit;
        auto actual_length = static_cast<USHORT>(ret.actual_length);

        if (actual_length <= TransferBufferLength) {
                TransferBufferLength = actual_length;
        } else {
                TransferBufferLength = 0;
                return ERR_GENERAL;
        }

        return ret.status ? ERR_GENERAL : ERR_NONE;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto read_descr(
        _In_ device_ctx_ext &dev, _In_ UCHAR type, _In_ UCHAR index, _In_ USHORT lang_id, 
        _In_ memory pool, _Out_ void *dest, _Inout_ USHORT &len)
{
        PAGED_CODE();

        if (auto err = read_descr_hdr(dev, type, index, lang_id, len)) {
                return err;
        }

        if (auto err = recv(dev.sock, pool, dest, len)) {
                Trace(TRACE_LEVEL_ERROR, "%!usb_descriptor_type!, length %d -> %!STATUS!", type, len, err);
                return ERR_NETWORK;
        }

        return ERR_NONE;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto read_device_descr(_In_ device_ctx_ext &dev)
{
        PAGED_CODE();

        auto &dd = dev.descriptor;
        USHORT len = sizeof(dd);

        if (auto err = read_descr(dev, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, memory::nonpaged, &dd, len)) {
                return err;
        }

        return len == sizeof(dd) && usbdlib::is_valid(dd) ? ERR_NONE : ERR_GENERAL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto read_config_descr(
        _Inout_ device_ctx_ext &dev, _In_ UCHAR index, _In_ memory pool, 
        _Out_ USB_CONFIGURATION_DESCRIPTOR *cd, _Inout_ USHORT &len)
{
        PAGED_CODE();
        NT_ASSERT(len >= sizeof(*cd));

        if (auto err = read_descr(dev, USB_CONFIGURATION_DESCRIPTOR_TYPE, index, 0, pool, cd, len)) {
                return err;
        }

        return len >= sizeof(*cd) && usbdlib::is_valid(*cd) ? ERR_NONE : ERR_GENERAL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto read_config_descr(_Outptr_ USB_CONFIGURATION_DESCRIPTOR* &cfg, _In_ device_ctx_ext &dev, _In_ UCHAR index)
{
        PAGED_CODE();

        USB_CONFIGURATION_DESCRIPTOR cd{};
        USHORT len = sizeof(cd);

        if (auto err = read_config_descr(dev, index, memory::stack, &cd, len)) {
                return err;
        }

        log(cd);
        len = cd.wTotalLength;

        NT_ASSERT(!cfg);
        cfg = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, len, POOL_TAG);
        if (!cfg) {
                return ERR_GENERAL;
        }

        if (auto err = read_config_descr(dev, index, memory::nonpaged, cfg, len)) {
                return err;
        }

        return len == cd.wTotalLength ? ERR_NONE : ERR_GENERAL;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto import_remote_device(_Out_ usbip_usb_device &udev, _Inout_ device_ctx_ext &ext)
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

        udev = reply.udev;
        log(udev);

        if (auto d = &ext.dev) {
                d->devid = make_devid(static_cast<UINT16>(udev.busnum), static_cast<UINT16>(udev.devnum));
                d->speed = static_cast<usb_device_speed>(udev.speed);
                d->vendor = udev.idVendor;
                d->product = udev.idProduct;
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
PAGED auto start_device(_In_ int &port, _In_ UDECXUSBDEVICE device)
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

        usbip_usb_device udev;
        if (bool(error = import_remote_device(udev, *ext.ptr))) {
                return;
        }

        UDECXUSBDEVICE dev{};
        if (NT_ERROR(device::create(dev, udev, vhci, ext.ptr))) {
                return;
        }
        ext.release(); // now dev owns it

        if (auto err = start_device(r.port, dev)) {
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
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), reinterpret_cast<PVOID*>(&r), nullptr)) {
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

        size_t buf_sz = 0;
        vhci::ioctl_imported_dev *result{};
        if (auto err = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result), reinterpret_cast<PVOID*>(&result), &buf_sz)) {
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
                if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*hdr), reinterpret_cast<PVOID*>(&hdr), nullptr))) {
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
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchSequential);
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

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::add_descriptors(
        _In_ _UDECXUSBDEVICE_INIT *init, _In_ device_ctx_ext &dev, _In_ const usbip_usb_device &udev)
{
        PAGED_CODE();

        if (auto err = read_device_descr(dev)) {
                return err;
        }

        log(dev.descriptor);

        if (!is_same_device(udev, dev.descriptor)) {
                Trace(TRACE_LEVEL_ERROR, "USB_DEVICE_DESCRIPTOR mismatches op_import_reply.udev");
                return ERR_GENERAL;
        }

        if (auto err = UdecxUsbDeviceInitAddDescriptor(init, reinterpret_cast<UCHAR*>(&dev.descriptor), dev.descriptor.bLength)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceInitAddDescriptor %!STATUS!", err);
                return err;
        }

        for (UCHAR i = 0; i < dev.descriptor.bNumConfigurations; ++i) {

                USB_CONFIGURATION_DESCRIPTOR *cd{};
                if (auto err = read_config_descr(cd, dev, i)) {
                        return err;
                }

                TraceDbg("USB_CONFIGURATION_DESCRIPTOR: %!BIN!", WppBinary(cd, cd->wTotalLength));
                auto err = UdecxUsbDeviceInitAddDescriptorWithIndex(init, reinterpret_cast<UCHAR*>(cd), cd->wTotalLength, i);

                if (i) {
                        ExFreePoolWithTag(cd, POOL_TAG);
                } else {
                        NT_ASSERT(!dev.actconfig);
                        dev.actconfig = cd;
                }

                if (err) {
                        Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceInitAddDescriptorWithIndex(%d) %!STATUS!", err, i);
                        return err;
                }
        }

        return ERR_NONE;
}
