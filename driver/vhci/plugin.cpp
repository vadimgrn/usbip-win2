#include "plugin.h"
#include "trace.h"
#include "plugin.tmh"

#include "vhub.h"
#include "pnp.h"
#include "usb_util.h"
#include "usbdsc.h"
#include "vhci.h"
#include "pnp_remove.h"
#include "irp.h"
#include "csq.h"
#include "usbip_proto_op.h"
#include "usbip_network.h"
#include "proto.h"
#include "ch9.h"
#include "dbgcommon.h"
#include "pdu.h"
#include "strutil.h"

namespace
{

constexpr unsigned int make_devid(int busnum, int devnum)
{
        return (busnum << 16) | devnum;
}

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

inline void log(const USB_CONFIGURATION_DESCRIPTOR &d)
{
        Trace(TRACE_LEVEL_VERBOSE,
                "ConfigurationDescriptor(bLength %d, %!usb_descriptor_type!, wTotalLength %hu(%#x), "
                "bNumInterfaces %d, bConfigurationValue %d, iConfiguration %d, bmAttributes %#x, MaxPower %d)",
                d.bLength, d.bDescriptorType, d.wTotalLength, d.wTotalLength,
                d.bNumInterfaces, d.bConfigurationValue, d.iConfiguration, d.bmAttributes, d.MaxPower);
}

PAGEABLE auto is_same(const usbip_usb_device &dev, const USB_DEVICE_DESCRIPTOR &dsc)
{
        return  dev.idVendor == dsc.idVendor &&
                dev.idProduct == dsc.idProduct &&
                dev.bcdDevice == dsc.bcdDevice &&

                dev.bDeviceClass == dsc.bDeviceClass &&
                dev.bDeviceSubClass == dsc.bDeviceSubClass &&
                dev.bDeviceProtocol == dsc.bDeviceProtocol &&

                dev.bNumConfigurations == dsc.bNumConfigurations;
}

PAGEABLE auto is_same(const usbip_usb_device &dev, const USB_CONFIGURATION_DESCRIPTOR &cd)
{
        return  dev.bConfigurationValue == cd.bConfigurationValue &&
                dev.bNumInterfaces == cd.bNumInterfaces;
}

PAGEABLE auto is_configured(const usbip_usb_device &d)
{
        return d.bConfigurationValue && d.bNumInterfaces;
}

PAGEABLE auto copy(UNICODE_STRING &dst, const char *src)
{
        PAGED_CODE();

        UTF8_STRING s;
        RtlInitUTF8String(&s, src);

        return RtlUTF8StringToUnicodeString(&dst, &s, true);
}

PAGEABLE auto copy(vpdo_dev_t *vpdo, const ioctl_usbip_vhci_plugin &r)
{
        PAGED_CODE();

        vpdo->busid = libdrv_strdup(POOL_FLAG_NON_PAGED, r.busid);
        if (!vpdo->busid) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error", r.busid);
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (auto err = copy(vpdo->node_name, r.host)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.host, err);
                return err;
        }

        if (auto err = copy(vpdo->service_name, r.service)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.service, err);
                return err;
        }

        if (!*r.serial) {
                RtlInitUnicodeString(&vpdo->serial, nullptr);
        } else if (auto err = copy(vpdo->serial, r.serial)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.serial, err);
                return err;
        }

        return STATUS_SUCCESS;
}

PAGEABLE auto init(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo.current_intf_num);
        NT_ASSERT(!vpdo.current_intf_alt);

        NT_ASSERT(vpdo.PnPState == pnp_state::NotStarted);
        NT_ASSERT(vpdo.PreviousPnPState == pnp_state::NotStarted);

	// vpdo usually starts its life at D3
	vpdo.DevicePowerState = PowerDeviceD3;
	vpdo.SystemPowerState = PowerSystemWorking;

	if (auto err = init_queues(vpdo)) {
		Trace(TRACE_LEVEL_ERROR, "init_queues -> %!STATUS!", err);
		return err;
	}

	auto &Flags = vpdo.Self->Flags;
	Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	if (!vhub_attach_vpdo(&vpdo)) {
		Trace(TRACE_LEVEL_ERROR, "Can't acquire free usb port");
		return STATUS_END_OF_FILE;
	}

        Flags &= ~DO_DEVICE_INITIALIZING; // should be the last step in initialization
	return STATUS_SUCCESS;
}

PAGEABLE auto init(vpdo_dev_t &vpdo, const USB_DEVICE_DESCRIPTOR &d)
{
	PAGED_CODE();

	if (is_valid_dsc(&d)) {
		NT_ASSERT(!is_valid_dsc(&vpdo.descriptor)); // first time initialization
		RtlCopyMemory(&vpdo.descriptor, &d, sizeof(d));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid device descriptor");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo.speed = get_usb_speed(d.bcdUSB);

	vpdo.bDeviceClass = d.bDeviceClass;
	vpdo.bDeviceSubClass = d.bDeviceSubClass;
	vpdo.bDeviceProtocol = d.bDeviceProtocol;

	return STATUS_SUCCESS;
}

PAGEABLE auto set_class_subclass_proto(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	auto d = dsc_find_next_intf(vpdo.actconfig, nullptr);
	if (!d) {
		Trace(TRACE_LEVEL_ERROR, "Interface descriptor not found");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo.bDeviceClass = d->bInterfaceClass;
	vpdo.bDeviceSubClass = d->bInterfaceSubClass;
	vpdo.bDeviceProtocol = d->bInterfaceProtocol;

	Trace(TRACE_LEVEL_INFORMATION, "Set Class(%#02x)/SubClass(%#02x)/Protocol(%#02x) from bInterfaceNumber %d, bAlternateSetting %d",
					vpdo.bDeviceClass, vpdo.bDeviceSubClass, vpdo.bDeviceProtocol,
					d->bInterfaceNumber, d->bAlternateSetting);

	return STATUS_SUCCESS;
}

/*
* Many devices have zero usb class number in a device descriptor.
* zero value means that class number is determined at interface level.
* USB class, subclass and protocol numbers should be setup before importing.
* Because windows vhci driver builds a device compatible id with those numbers.
*/
PAGEABLE auto init(vpdo_dev_t &vpdo, const USB_CONFIGURATION_DESCRIPTOR &d)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo.actconfig); // first time initialization
	vpdo.actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED|POOL_FLAG_UNINITIALIZED, 
                                                                        d.wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo.actconfig) {
		RtlCopyMemory(vpdo.actconfig, &d, d.wTotalLength);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate %d bytes of memory", d.wTotalLength);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	return d.bNumInterfaces == 1 && !(vpdo.bDeviceClass || vpdo.bDeviceSubClass || vpdo.bDeviceProtocol) ?
		set_class_subclass_proto(vpdo) : STATUS_SUCCESS;
}

PAGEABLE NTSTATUS create_vpdo(vpdo_dev_t* &result, vhci_dev_t *vhci, const ioctl_usbip_vhci_plugin &r)
{
        PAGED_CODE();

        result = nullptr;

        auto devobj = vdev_create(vhci->Self->DriverObject, VDEV_VPDO);
        if (!devobj) {
                return STATUS_UNSUCCESSFUL;
        }

        auto vpdo = to_vpdo_or_null(devobj);
        vpdo->parent = vhub_from_vhci(vhci);

        if (auto err = copy(vpdo, r)) {
                return err;
        }

        result = vpdo;
        return STATUS_SUCCESS;

/*
        vpdo->devid = r.devid;

        if (auto err = init(*vpdo, r.dscr_dev)) {
                destroy_device(vpdo);
                return err;
        }

        if (auto err = init(*vpdo, r.dscr_conf)) {
                destroy_device(vpdo);
                return err;
        }

        if (auto err = init(*vpdo)) {
                destroy_device(vpdo);
                return err;
        }

        NT_ASSERT(vpdo->port > 0); // was assigned
        r.port = vpdo->port;
        outlen = sizeof(r.port);

        IoInvalidateDeviceRelations(vhci->pdo, BusRelations); // kick PnP system
        return STATUS_SUCCESS;
*/
}

PAGEABLE auto send_req_import(wsk::SOCKET *sock, const char *busid)
{
        PAGED_CODE();

        struct 
        {
                op_common hdr{ USBIP_VERSION, OP_REQ_IMPORT, ST_OK };
                op_import_request body;
        } req;

        static_assert(sizeof(req) == sizeof(req.hdr) + sizeof(req.body)); // packed

        strcpy_s(req.body.busid, sizeof(req.body.busid), busid);

        PACK_OP_COMMON(0, &req.hdr);
        PACK_OP_IMPORT_REQUEST(0, &req.body);

        return send(sock, usbip::memory::stack, &req, sizeof(req)) ? ERR_NONE : ERR_NETWORK;
}

PAGEABLE auto recv_rep_import(wsk::SOCKET *sock, const char *busid, usbip::memory pool, op_import_reply &reply)
{
        PAGED_CODE();

        if (auto err = usbip::recv_op_common(sock, OP_REP_IMPORT)) {
                Trace(TRACE_LEVEL_ERROR, "OP_REP_IMPORT(op_common) error %!op_status_t!", err);
                return err;
        }

        if (!recv(sock, pool, &reply, sizeof(reply))) {
                Trace(TRACE_LEVEL_ERROR, "Failed to recv OP_REP_IMPORT(op_import_reply)");
                return ERR_NETWORK;
        }

        PACK_OP_IMPORT_REPLY(0, &reply);

        if (strncmp(reply.udev.busid, busid, sizeof(reply.udev.busid))) {
                Trace(TRACE_LEVEL_ERROR, "Received busid(%s) != expected(%s)", reply.udev.busid, busid);
                return ERR_PROTOCOL;
        }

        return ERR_NONE;
}

PAGEABLE auto init_req_get_descr(usbip_header &hdr, vpdo_dev_t &vpdo, UCHAR type, USHORT TransferBufferLength)
{
        PAGED_CODE();

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

        if (auto err = set_cmd_submit_usbip_header(&vpdo, &hdr, EP0, TransferFlags, TransferBufferLength)) {
                return err == STATUS_INVALID_PARAMETER ? ERR_INVARG : ERR_GENERAL;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_GET_DESCRIPTOR;
        pkt->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(type, 0);
        pkt->wIndex.W = 0; // Zero or Language ID for string descriptor
        pkt->wLength = TransferBufferLength;

        return ERR_NONE;
}

PAGEABLE auto read_descr_hdr(wsk::SOCKET *sock, vpdo_dev_t &vpdo, UCHAR type, USHORT TransferBufferLength)
{
        PAGED_CODE();

        usbip_header hdr{};
        if (auto err = init_req_get_descr(hdr, vpdo, type, TransferBufferLength)) {
                return err;
        }

        char buf[DBG_USBIP_HDR_BUFSZ];
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "OUT %Iu%s", get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr));

        byteswap_header(hdr, swap_dir::host2net);

        if (!send(sock, usbip::memory::stack, &hdr, sizeof(hdr))) {
                Trace(TRACE_LEVEL_ERROR, "Send hdr error: %!usb_descriptor_type!", type);
                return ERR_NETWORK;
        }

        if (!recv(sock, usbip::memory::stack, &hdr, sizeof(hdr))) {
                Trace(TRACE_LEVEL_ERROR, "Recv hdr error: %!usb_descriptor_type!", type);
                return ERR_NETWORK;
        }

        byteswap_header(hdr, swap_dir::net2host);
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "IN %Iu%s", get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr));

        auto &b = hdr.base;
        if (!(b.command == USBIP_RET_SUBMIT && b.seqnum == vpdo.seqnum)) {
                return ERR_PROTOCOL;
        }

        auto &ret = hdr.u.ret_submit;
        return !ret.status && ret.actual_length == TransferBufferLength ? ERR_NONE : ERR_GENERAL;
}

PAGEABLE auto read_descr(usbip::memory pool, USB_DEVICE_DESCRIPTOR &dd, wsk::SOCKET *sock, vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        if (auto err = read_descr_hdr(sock, vpdo, USB_DEVICE_DESCRIPTOR_TYPE, sizeof(dd))) {
                return err;
        }

        if (!recv(sock, pool, &dd, sizeof(dd))) {
                return ERR_NETWORK;
        }

        return is_valid_dsc(&dd) ? ERR_NONE : ERR_GENERAL;
}

PAGEABLE auto read_descr(USB_CONFIGURATION_DESCRIPTOR* &cfgd, wsk::SOCKET *sock, vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        USB_CONFIGURATION_DESCRIPTOR cd{};
        if (auto err = read_descr_hdr(sock, vpdo, USB_CONFIGURATION_DESCRIPTOR_TYPE, sizeof(cd))) {
                return err;
        }

        if (!recv(sock, usbip::memory::stack, &cd, sizeof(cd))) {
                return ERR_NETWORK;
        }

        cfgd = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED|POOL_FLAG_UNINITIALIZED, cd.wTotalLength, USBIP_VHCI_POOL_TAG);
        if (!cfgd) {
                return ERR_GENERAL;
        }

        if (auto err = read_descr_hdr(sock, vpdo, USB_CONFIGURATION_DESCRIPTOR_TYPE, cd.wTotalLength)) {
                return err;
        }

        if (!recv(sock, usbip::memory::nonpaged, cfgd, cd.wTotalLength)) {
                return ERR_NETWORK;
        }

        return is_valid_cfg_dsc(cfgd) ? ERR_NONE : ERR_GENERAL;
}

PAGEABLE auto process(wsk::SOCKET *sock, const char *busid)
{
        PAGED_CODE();

        if (auto err = send_req_import(sock, busid)) {
                Trace(TRACE_LEVEL_ERROR, "OP_REQ_IMPORT %!err_t!", err);
                return err;
        }

        op_import_reply reply{};
        if (auto err = recv_rep_import(sock, busid, usbip::memory::stack, reply)) {
                return err;
        }

        log(reply.udev);

        vpdo_dev_t vpdo{};
        vpdo.devid = make_devid(reply.udev.busnum, reply.udev.devnum);

        USB_DEVICE_DESCRIPTOR dd{};
        if (auto err = read_descr(usbip::memory::stack, dd, sock, vpdo)) {
                return err;
        }

        log(dd);

        if (!is_same(reply.udev, dd)) {
                Trace(TRACE_LEVEL_ERROR, "USB_DEVICE_DESCRIPTOR mismatches op_import_reply.udev");
                return ERR_GENERAL;
        }

        USB_CONFIGURATION_DESCRIPTOR *cd{};
        if (auto err = read_descr(cd, sock, vpdo)) {
                if (cd) {
                        ExFreePoolWithTag(cd, USBIP_VHCI_POOL_TAG);
                }
                return err;
        }

        TraceUrb("USB_CONFIGURATION_DESCRIPTOR: %!BIN!", WppBinary(cd, cd->wTotalLength));
        log(*cd);

        if (is_configured(reply.udev) && !is_same(reply.udev, *cd)) {
                Trace(TRACE_LEVEL_ERROR, "USB_CONFIGURATION_DESCRIPTOR mismatches op_import_reply.udev");
                ExFreePoolWithTag(cd, USBIP_VHCI_POOL_TAG);
                return ERR_GENERAL;
        }

        ExFreePoolWithTag(cd, USBIP_VHCI_POOL_TAG);
        return ERR_NONE;
}

PAGEABLE auto getaddrinfo(ADDRINFOEXW* &result, vpdo_dev_t *vpdo)
{
        PAGED_CODE();

        ADDRINFOEXW hints{};
        hints.ai_flags = AI_NUMERICSERV;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP; // zero isn't work

        return wsk::getaddrinfo(result, &vpdo->node_name, &vpdo->service_name, &hints);
}

PAGEABLE auto set_options(wsk::SOCKET *sock)
{
        PAGED_CODE();

        enum { KEEPIDLE = 30, KEEPCNT = 9, KEEPINTVL = 10 };

        if (auto err = set_keepalive(sock, KEEPIDLE, KEEPCNT, KEEPINTVL)) {
                Trace(TRACE_LEVEL_ERROR, "set_keepalive %!STATUS!", err);
                return err;
        }

        bool optval{};
        if (auto err = get_keepalive(sock, optval)) {
                Trace(TRACE_LEVEL_ERROR, "get_keepalive %!STATUS!", err);
                return err;
        } else {
                Trace(TRACE_LEVEL_VERBOSE, "keepalive %d", optval);
        }

        int idle = 0;
        int cnt = 0;
        int intvl = 0;

        if (auto err = get_keepalive_opts(sock, &idle, &cnt, &intvl)) {
                Trace(TRACE_LEVEL_ERROR, "get_keepalive_opts %!STATUS!", err);
                return err;
        }

        auto timeout = idle + cnt*intvl;
        Trace(TRACE_LEVEL_VERBOSE, "keepalive: idle(%d sec) + cnt(%d)*intvl(%d sec) => %d sec.", idle, cnt, intvl, timeout);

        return STATUS_SUCCESS;
}

PAGEABLE auto prepare_socket(wsk::SOCKET *sock, const ADDRINFOEXW &ai, void*)
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

        return connect(sock, ai.ai_addr);
}

PAGEABLE auto connect(vpdo_dev_t *vpdo)
{
        PAGED_CODE();

        ADDRINFOEXW *ai{};
        if (auto err = getaddrinfo(ai, vpdo)) {
                Trace(TRACE_LEVEL_ERROR, "getaddrinfo %!STATUS!", err);
                return err;
        }

        static WSK_CLIENT_CONNECTION_DISPATCH dispatch;
        
        NT_ASSERT(!vpdo->sock);
        vpdo->sock = wsk::for_each(WSK_FLAG_CONNECTION_SOCKET, vpdo, &dispatch, ai, prepare_socket, nullptr);

        wsk::free(ai);
        return vpdo->sock ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

} // namespace


/*
 * TCP_NODELAY => STATUS_NOT_SUPPORTED
 */
PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, ioctl_usbip_vhci_plugin &r)
{
	PAGED_CODE();
        TraceCall("%s:%s, busid %s, serial '%s'", r.host, r.service, r.busid, *r.serial ? r.serial : "");

        auto &error = r.port; // err_t
        error = ERR_GENERAL;

        vpdo_dev_t *vpdo{};
        if (auto err = create_vpdo(vpdo, vhci, r)) {
                destroy_device(vpdo);
                return err;
        }

        error = ERR_NETWORK;

        if (auto err = connect(vpdo)) {
                Trace(TRACE_LEVEL_ERROR, "Can't connect to a server");
                destroy_device(vpdo);
                return err;
        }

        Trace(TRACE_LEVEL_INFORMATION, "Connected to %!USTR!:%!USTR!", &vpdo->node_name, &vpdo->service_name);

        error = process(vpdo->sock, vpdo->busid);
        destroy_device(vpdo);

        return STATUS_NOT_IMPLEMENTED;
}

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
