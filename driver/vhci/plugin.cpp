#include "plugin.h"
#include <wdm.h>
#include "trace.h"
#include "plugin.tmh"

#include "vhub.h"
#include "usb_util.h"
#include "vhci.h"
#include "pnp_remove.h"
#include "csq.h"
#include "usbip_proto_op.h"
#include "network.h"
#include "proto.h"
#include "ch9.h"
#include "dbgcommon.h"
#include "pdu.h"
#include "strutil.h"
#include "wsk_events.h"
#include "pnp.h"

namespace
{

const WSK_CLIENT_CONNECTION_DISPATCH g_dispatch{ WskReceiveEvent, WskDisconnectEvent };

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

_IRQL_requires_max_(DISPATCH_LEVEL)
inline void log(const USB_DEVICE_DESCRIPTOR &d)
{
        Trace(TRACE_LEVEL_VERBOSE,
                "DeviceDescriptor(bLength %d, %!usb_descriptor_type!, bcdUSB %#x, "
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

_IRQL_requires_max_(DISPATCH_LEVEL)
inline void log(const USB_CONFIGURATION_DESCRIPTOR &d)
{
        Trace(TRACE_LEVEL_VERBOSE,
                "ConfigurationDescriptor(bLength %d, %!usb_descriptor_type!, wTotalLength %hu(%#x), "
                "bNumInterfaces %d, bConfigurationValue %d, iConfiguration %d, bmAttributes %#x, MaxPower %d)",
                d.bLength, d.bDescriptorType, d.wTotalLength, d.wTotalLength,
                d.bNumInterfaces, d.bConfigurationValue, d.iConfiguration, d.bmAttributes, d.MaxPower);
}


/*
 * RtlFreeUnicodeString must be used to release memory.
 * @see RtlUTF8StringToUnicodeString
 */
auto to_unicode_str(UNICODE_STRING &dst, const char *ansi)
{
        ANSI_STRING s;
        RtlInitAnsiString(&s, ansi);

        return RtlAnsiStringToUnicodeString(&dst, &s, true);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto is_same_device(const usbip_usb_device &dev, const USB_DEVICE_DESCRIPTOR &dsc)
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

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto is_same_device(const usbip_usb_device &dev, const USB_CONFIGURATION_DESCRIPTOR &cd)
{
        PAGED_CODE();

        return  dev.bConfigurationValue == cd.bConfigurationValue &&
                dev.bNumInterfaces == cd.bNumInterfaces;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto is_configured(const usbip_usb_device &d)
{
        PAGED_CODE();
        return d.bConfigurationValue && d.bNumInterfaces;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto init(vpdo_dev_t &vpdo, const ioctl_usbip_vhci_plugin &r)
{
        PAGED_CODE();

        vpdo.busid = libdrv_strdup(POOL_FLAG_NON_PAGED, r.busid);
        if (!vpdo.busid) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error", r.busid);
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (auto err = to_unicode_str(vpdo.node_name, r.host)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.host, err);
                return err;
        }

        if (auto err = to_unicode_str(vpdo.service_name, r.service)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.service, err);
                return err;
        }

        if (!*r.serial) {
                // RtlInitUnicodeString(&vpdo.serial, nullptr);
        } else if (auto err = to_unicode_str(vpdo.serial, r.serial)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.serial, err);
                return err;
        }

        return STATUS_SUCCESS;
}

/*
 * Many devices have zero usb class number in a device descriptor.
 * zero value means that class number is determined at interface level.
 * USB class, subclass and protocol numbers should be setup before importing.
 * Because windows vhci driver builds a device compatible id with those numbers.
 */
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto set_class_subclass_proto(vpdo_dev_t &vpdo)
{
	PAGED_CODE();
        NT_ASSERT(vpdo.actconfig);

        auto use_intf = vpdo.actconfig->bNumInterfaces == 1 && !(vpdo.bDeviceClass || vpdo.bDeviceSubClass || vpdo.bDeviceProtocol);
        if (!use_intf) {
                return true;
        }

	auto d = dsc_find_next_intf(vpdo.actconfig, nullptr);
	if (!d) {
		Trace(TRACE_LEVEL_ERROR, "Interface descriptor not found");
		return false;
	}

	vpdo.bDeviceClass = d->bInterfaceClass;
	vpdo.bDeviceSubClass = d->bInterfaceSubClass;
	vpdo.bDeviceProtocol = d->bInterfaceProtocol;

	Trace(TRACE_LEVEL_INFORMATION, "Set Class(%#02x)/SubClass(%#02x)/Protocol(%#02x) from bInterfaceNumber %d, bAlternateSetting %d",
					vpdo.bDeviceClass, vpdo.bDeviceSubClass, vpdo.bDeviceProtocol,
					d->bInterfaceNumber, d->bAlternateSetting);

	return true;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto create_vpdo(vpdo_dev_t* &vpdo, vhci_dev_t *vhci, const ioctl_usbip_vhci_plugin &r)
{
        PAGED_CODE();
        NT_ASSERT(!vpdo);

        auto devobj = vdev_create(vhci->Self->DriverObject, VDEV_VPDO);
        if (!devobj) {
                return make_error(ERR_GENERAL);
        }

        InterlockedIncrement(&VpdoCount);

        vpdo = to_vpdo_or_null(devobj);
        vpdo->parent = vhub_from_vhci(vhci);

        vpdo->DevicePowerState = PowerDeviceD3;
        vpdo->SystemPowerState = PowerSystemWorking;

        vpdo->Self->Flags |= DO_POWER_PAGABLE | DO_DIRECT_IO;
        NT_ASSERT(!is_valid_seqnum(vpdo->wsk_data_hdr.base.seqnum)); // see wsk_events.cpp, get_header

        if (init(*vpdo, r)) {
                return make_error(ERR_GENERAL);
        }

        if (auto err = init_queue(*vpdo)) {
                Trace(TRACE_LEVEL_ERROR, "init_queues %!STATUS!", err);
                return make_error(ERR_GENERAL);
        }

        return make_error(ERR_NONE);
}

/*
 * @see <linux>/tools/usb/usbip/src/usbipd.c, recv_request_import
 */
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto send_req_import(vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        struct 
        {
                op_common hdr{ USBIP_VERSION, OP_REQ_IMPORT, ST_OK };
                op_import_request body;
        } req;

        static_assert(sizeof(req) == sizeof(req.hdr) + sizeof(req.body)); // packed

        strcpy_s(req.body.busid, sizeof(req.body.busid), vpdo.busid);

        PACK_OP_COMMON(0, &req.hdr);
        PACK_OP_IMPORT_REQUEST(0, &req.body);

        return send(vpdo.sock, usbip::memory::stack, &req, sizeof(req));
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto recv_rep_import(vpdo_dev_t &vpdo, usbip::memory pool, op_import_reply &reply)
{
        PAGED_CODE();

        auto status = ST_OK;

        if (auto err = usbip::recv_op_common(vpdo.sock, OP_REP_IMPORT, status)) {
                return make_error(err);
        }

        if (status) {
                Trace(TRACE_LEVEL_ERROR, "OP_REP_IMPORT %!op_status_t!", status);
                return make_error(ERR_NONE, status);
        }

        if (auto err = recv(vpdo.sock, pool, &reply, sizeof(reply))) {
                Trace(TRACE_LEVEL_ERROR, "Receive op_import_reply %!STATUS!", err);
                return make_error(ERR_NETWORK);
        }

        PACK_OP_IMPORT_REPLY(0, &reply);

        if (strncmp(reply.udev.busid, vpdo.busid, sizeof(reply.udev.busid))) {
                Trace(TRACE_LEVEL_ERROR, "Received busid(%s) != expected(%s)", reply.udev.busid, vpdo.busid);
                return make_error(ERR_PROTOCOL);
        }

        return make_error(ERR_NONE);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto init_req_get_descr(
        _Out_ usbip_header &hdr, vpdo_dev_t &vpdo, UCHAR type, UCHAR index, USHORT lang_id, USHORT TransferBufferLength)
{
        PAGED_CODE();

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, TransferBufferLength)) {
                return false;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_GET_DESCRIPTOR;
        pkt->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(type, index);
        pkt->wIndex.W = lang_id; // Zero or Language ID for string descriptor
        pkt->wLength = TransferBufferLength;

        return true;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto read_descr_hdr(vpdo_dev_t &vpdo, UCHAR type, UCHAR index, USHORT lang_id, _Inout_ USHORT &TransferBufferLength)
{
        PAGED_CODE();

        usbip_header hdr{};
        if (!init_req_get_descr(hdr, vpdo, type, index, lang_id, TransferBufferLength)) {
                return ERR_GENERAL;
        }

        char buf[DBG_USBIP_HDR_BUFSZ];
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "OUT %Iu%s", get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr, true));

        byteswap_header(hdr, swap_dir::host2net);

        if (auto err = send(vpdo.sock, usbip::memory::stack, &hdr, sizeof(hdr))) {
                Trace(TRACE_LEVEL_ERROR, "Send header of %!usb_descriptor_type! %!STATUS!", type, err);
                return ERR_NETWORK;
        }

        if (auto err = recv(vpdo.sock, usbip::memory::stack, &hdr, sizeof(hdr))) {
                Trace(TRACE_LEVEL_ERROR, "Recv header of %!usb_descriptor_type! %!STATUS!", type, err);
                return ERR_NETWORK;
        }

        byteswap_header(hdr, swap_dir::net2host);
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "IN %Iu%s", get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr, true));

        auto &b = hdr.base;
        if (!(b.command == USBIP_RET_SUBMIT && extract_num(b.seqnum) == vpdo.seqnum)) {
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

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto read_descr(
        vpdo_dev_t &vpdo, UCHAR type, UCHAR index, USHORT lang_id, usbip::memory pool, _Out_ void *dest, _Inout_ USHORT &len)
{
        PAGED_CODE();

        if (auto err = read_descr_hdr(vpdo, type, index, lang_id, len)) {
                return err;
        }

        if (auto err = recv(vpdo.sock, pool, dest, len)) {
                Trace(TRACE_LEVEL_ERROR, "%!usb_descriptor_type!, length %d -> %!STATUS!", type, len, err);
                return ERR_NETWORK;
        }

        return ERR_NONE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto read_device_descr(vpdo_dev_t &vpdo)
{
        PAGED_CODE();
        
        USHORT len = sizeof(vpdo.descriptor);

        if (auto err = read_descr(vpdo, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, usbip::memory::nonpaged, &vpdo.descriptor, len)) {
                return err;
        }

        return len == sizeof(vpdo.descriptor) && is_valid(vpdo.descriptor) ? ERR_NONE : ERR_GENERAL;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto read_config_descr(
        _Inout_ vpdo_dev_t &vpdo, _In_ usbip::memory pool, _Out_ USB_CONFIGURATION_DESCRIPTOR *cd, _Inout_ USHORT &len)
{
        PAGED_CODE();
        NT_ASSERT(len >= sizeof(*cd));

        if (auto err = read_descr(vpdo, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, pool, cd, len)) {
                return err;
        }

        return len >= sizeof(*cd) && is_valid(*cd) ? ERR_NONE : ERR_GENERAL;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto read_config_descr(vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        USB_CONFIGURATION_DESCRIPTOR cd{};
        USHORT len = sizeof(cd);

        if (auto err = read_config_descr(vpdo, usbip::memory::stack, &cd, len)) {
                return err;
        }

        log(cd);
        len = cd.wTotalLength;

        NT_ASSERT(!vpdo.actconfig);
        vpdo.actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, 
                                                                        len, USBIP_VHCI_POOL_TAG);

        if (!vpdo.actconfig) {
                return ERR_GENERAL;
        }

        if (auto err = read_config_descr(vpdo, usbip::memory::nonpaged, vpdo.actconfig, len)) {
                return err;
        }

        return len == cd.wTotalLength ? ERR_NONE : ERR_GENERAL;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void read_os_string_descriptor(vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        USB_OS_STRING_DESCRIPTOR d;
        USHORT len = sizeof(d);

        if (auto err = read_descr(vpdo, USB_STRING_DESCRIPTOR_TYPE, MS_OS_STRING_DESC_INDEX, 0, usbip::memory::stack, &d, len)) {
                return;
        }

        if (len != sizeof(d)) {
                return;
        }

        UNICODE_STRING sig{ sizeof(d.Signature), sizeof(d.Signature), d.Signature };

        TraceDbg("bLength %d, %!usb_descriptor_type!, Signature '%!USTR!', MS_VendorCode %#x, Pad %d", 
                  d.bLength, d.bDescriptorType, &sig, d.MS_VendorCode, d.Pad);

        if (is_valid(d)) {
                vpdo.MS_VendorCode = d.MS_VendorCode;
        }
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto read_string_descriptors(vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        USHORT lang_id = 0;

        for (UCHAR idx = 0; idx < ARRAYSIZE(vpdo.strings); ++idx) {

                USB_STRING_DESCRIPTOR hdr;
                USHORT len = sizeof(hdr);

                if (auto err = read_descr(vpdo, USB_STRING_DESCRIPTOR_TYPE, idx, lang_id, usbip::memory::stack, &hdr, len)) {
                        break; // invalid string index, EPIPE
                }

                if (!(len >= sizeof(USB_COMMON_DESCRIPTOR) && is_valid(hdr))) { // string length can be zero
                        Trace(TRACE_LEVEL_ERROR, "USB_STRING_DESCRIPTOR expected, length %d", len);
                        return ERR_GENERAL;
                }

                len = hdr.bLength;
                if (len == sizeof(USB_COMMON_DESCRIPTOR)) {
                        Trace(TRACE_LEVEL_WARNING, "Index %d, skip empty string", idx);
                        continue;
                }

                auto sz = len + sizeof(*hdr.bString); // + L'\0'

                auto sd = (USB_STRING_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, sz, USBIP_VHCI_POOL_TAG);
                if (!sd) {
                        Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", sz);
                        return ERR_GENERAL;
                }

                if (len <= sizeof(hdr)) {
                        RtlCopyMemory(sd, &hdr, len);
                } else if (auto err = read_descr(vpdo, USB_STRING_DESCRIPTOR_TYPE, idx, lang_id, usbip::memory::nonpaged, sd, len)) {
                        ExFreePoolWithTag(sd, USBIP_VHCI_POOL_TAG);
                        return err;
                }

                if (len == hdr.bLength && is_valid(*sd) && sd->bLength == len) {
                        *reinterpret_cast<wchar_t*>((char*)sd + sd->bLength) = L'\0';
                        vpdo.strings[idx] = sd;
                } else {
                        Trace(TRACE_LEVEL_ERROR, "length %d", len);
                        ExFreePoolWithTag(sd, USBIP_VHCI_POOL_TAG);
                        return ERR_GENERAL;
                }

                if (idx) {
                        TraceMsg("Index %d, LangId %#x, '%!WSTR!'", idx, lang_id, sd->bString);
                } else {
                        TraceMsg("List of supported languages%!BIN!", WppBinary(sd, sd->bLength));
                        lang_id = *hdr.bString; // Supported Language Code Zero, f.e. 0x0409 English - United States
                }
        }

        return ERR_NONE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE void init(vpdo_dev_t &vpdo, const USB_DEVICE_DESCRIPTOR &d)
{
        PAGED_CODE();

        vpdo.speed = get_usb_speed(d.bcdUSB);

        vpdo.bDeviceClass = d.bDeviceClass;
        vpdo.bDeviceSubClass = d.bDeviceSubClass;
        vpdo.bDeviceProtocol = d.bDeviceProtocol;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto fetch_descriptors(vpdo_dev_t &vpdo, const usbip_usb_device &udev)
{
        PAGED_CODE();

        if (auto err = read_device_descr(vpdo)) {
                return err;
        }

        log(vpdo.descriptor);

        if (is_same_device(udev, vpdo.descriptor)) {
                init(vpdo, vpdo.descriptor);
        } else {
                Trace(TRACE_LEVEL_ERROR, "USB_DEVICE_DESCRIPTOR mismatches op_import_reply.udev");
                return ERR_GENERAL;
        }

        if (auto err = read_config_descr(vpdo)) {
                if (auto &ptr = vpdo.actconfig) {
                        ExFreePoolWithTag(ptr, USBIP_VHCI_POOL_TAG);
                        ptr = nullptr;
                }
                return err;
        }

        TraceDbg("USB_CONFIGURATION_DESCRIPTOR: %!BIN!", WppBinary(vpdo.actconfig, vpdo.actconfig->wTotalLength));

        if (is_configured(udev) && !is_same_device(udev, *vpdo.actconfig)) {
                Trace(TRACE_LEVEL_ERROR, "USB_CONFIGURATION_DESCRIPTOR mismatches op_import_reply.udev");
                return ERR_GENERAL;
        }

        if (!set_class_subclass_proto(vpdo)) {
                return ERR_GENERAL;
        }
        
        if (auto err = read_string_descriptors(vpdo)) {
                return err;
        }

        read_os_string_descriptor(vpdo);
        return ERR_NONE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto make_event_mask()
{
        PAGED_CODE();
        ULONG mask = 0;

        for (auto evt: WskEvents) {
                mask |= evt;
        }

        return mask;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto import_remote_device(vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        if (auto err = send_req_import(vpdo)) {
                Trace(TRACE_LEVEL_ERROR, "Send OP_REQ_IMPORT %!STATUS!", err);
                return make_error(ERR_NETWORK);
        }

        op_import_reply reply{};

        if (auto err = recv_rep_import(vpdo, usbip::memory::stack, reply)) { // result made by make_error()
                return err;
        }

        auto &udev = reply.udev;
        log(udev);

        vpdo.devid = make_devid(static_cast<UINT16>(udev.busnum), static_cast<UINT16>(udev.devnum));

        if (auto err = fetch_descriptors(vpdo, udev)) {
                return make_error(err);
        }

        auto event_mask = make_event_mask();

        if (auto err = event_callback_control(vpdo.sock, event_mask, false)) {
                Trace(TRACE_LEVEL_ERROR, "event_callback_control %!STATUS!", err);
                return make_error(ERR_NETWORK);
        }

        return make_error(ERR_NONE);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto getaddrinfo(ADDRINFOEXW* &result, vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        ADDRINFOEXW hints{};
        hints.ai_flags = AI_NUMERICSERV;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP; // zero isn't work

        return wsk::getaddrinfo(result, &vpdo.node_name, &vpdo.service_name, &hints);
}

/*
 * TCP_NODELAY is not supported, see WSK_FLAG_NODELAY.
 */
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto set_options(wsk::SOCKET *sock)
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

_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto try_connect(wsk::SOCKET *sock, const ADDRINFOEXW &ai, void*)
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
                Trace(TRACE_LEVEL_ERROR, "address %!BIN! -> %!STATUS!", 
                        WppBinary(ai.ai_addr, static_cast<USHORT>(ai.ai_addrlen)), err);
        }

        return err;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto connect(vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        ADDRINFOEXW *ai{};
        if (auto err = getaddrinfo(ai, vpdo)) {
                Trace(TRACE_LEVEL_ERROR, "getaddrinfo %!STATUS!", err);
                return make_error(ERR_NETWORK);
        }

        NT_ASSERT(!vpdo.sock);
        vpdo.sock = wsk::for_each(WSK_FLAG_CONNECTION_SOCKET, &vpdo, &g_dispatch, ai, try_connect, nullptr);

        wsk::free(ai);
        return make_error(vpdo.sock ? ERR_NONE : ERR_NETWORK);
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(yes))
PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, ioctl_usbip_vhci_plugin &r)
{
	PAGED_CODE();
        TraceMsg("%s:%s, busid %s, serial '%s'", r.host, r.service, r.busid, *r.serial ? r.serial : "");

        auto &error = r.port;

        vpdo_dev_t *vpdo{};
        if (bool(error = create_vpdo(vpdo, vhci, r))) {
                destroy_device(vpdo);
                return STATUS_SUCCESS;
        }

        if (bool(error = connect(*vpdo))) {
                Trace(TRACE_LEVEL_ERROR, "Can't connect to %!USTR!:%!USTR!", &vpdo->node_name, &vpdo->service_name);
                destroy_device(vpdo);
                return STATUS_SUCCESS;
        }

        Trace(TRACE_LEVEL_INFORMATION, "Connected to %!USTR!:%!USTR!", &vpdo->node_name, &vpdo->service_name);

        if (bool(error = import_remote_device(*vpdo))) {
                destroy_device(vpdo);
                return STATUS_SUCCESS;
        }

        if (vhub_attach_vpdo(vpdo)) {
                r.port = vpdo->port;
        } else {
                error = make_error(ERR_PORTFULL);
                Trace(TRACE_LEVEL_ERROR, "Can't acquire free usb port");
                destroy_device(vpdo);
                return STATUS_SUCCESS;
        }

        vpdo->Self->Flags &= ~DO_DEVICE_INITIALIZING; // must be the last step in initialization
        IoInvalidateDeviceRelations(vhci->pdo, BusRelations); // kick PnP system

        return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS vhci_unplug_vpdo(vhci_dev_t *vhci, int port)
{
	PAGED_CODE();

	auto vhub = vhub_from_vhci(vhci);
	if (!vhub) {
		Trace(TRACE_LEVEL_INFORMATION, "vhub has gone");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port <= 0) {
		Trace(TRACE_LEVEL_VERBOSE, "Plugging out all devices");
		vhub_unplug_all_vpdo(vhub);
		return STATUS_SUCCESS;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, port)) {
		return vhub_unplug_vpdo(vpdo);
	}

	Trace(TRACE_LEVEL_ERROR, "Invalid or empty port %d", port);
	return STATUS_NO_SUCH_DEVICE;
}
