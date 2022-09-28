/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

#include <libdrv\pageable.h>
#include <usbip\ch9.h>

#include <initguid.h>
#include <usbip\vhci.h>

/*
 * Macro WDF_TYPE_NAME_TO_TYPE_INFO (see WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE)
 * makes impossible to declare context type with the same name in different namespaces.
 */

namespace wsk
{
        struct SOCKET;
}

namespace usbip
{

/*
 * Device context for WDFDEVICE, Virtual Host Controller Interface.
 * Parent is WDFDRIVER.
 */
struct vhci_ctx
{
        WDFQUEUE queue; // default

        UDECXUSBDEVICE devices[vhci::TOTAL_PORTS]; // do not access directly, functions must be used
        WDFSPINLOCK devices_lock;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(vhci_ctx, get_vhci_ctx)


struct device_ctx;

/*
 * Context extention for device_ctx. 
 *
 * TCP/IP connection must be established before creation of UDECXUSBDEVICE because UdecxUsbDeviceInitSetSpeed 
 * must be called prior UdecxUsbDeviceCreate. So, these data can't be stored in device_ctx. 
 * The server's response on command OP_REQ_IMPORT contains required usbip_usb_device.speed. 
 * 
 * device_ctx_ext can't be embedded into device_ctx because SocketContext must be passed to WskSocket(). 
 * Pointer to instance of device_ctx_ext will be passed.
 * 
 * Alternative is to claim portnum in vhci_ctx.devices and pass it as SocketContext.
 */
struct device_ctx_ext
{
        device_ctx *ctx;
        wsk::SOCKET *sock;

        // from vhci::ioctl_plugin
        PSTR busid;
        UNICODE_STRING node_name;
        UNICODE_STRING service_name;
        UNICODE_STRING serial; // user-defined
        //
        
        vhci::ioctl_imported_dev_data dev; // for ioctl_imported_dev
};

/*
 * Device context for UDECXUSBDEVICE.
 */
struct device_ctx
{
        device_ctx_ext *ext;

        WDFDEVICE vhci; // parent
        UDECXUSBENDPOINT ep0; // default control pipe

        int port; // vhci_ctx.devices[port - 1], unique device id, this is not roothub's port number
        seqnum_t seqnum; // @see next_seqnum
        bool destroyed;
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(device_ctx, get_device_ctx)

/*
* Device context for UDECXUSBENDPOINT.
*/
struct endpoint_ctx
{
        UDECXUSBDEVICE device; // parent
        WDFQUEUE queue; // parent is UDECXUSBENDPOINT
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(endpoint_ctx, get_endpoint_ctx)

struct request_ctx
{
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(request_ctx, get_request_ctx)


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
seqnum_t next_seqnum(_Inout_ device_ctx &udev, _In_ bool dir_in);

constexpr auto extract_num(seqnum_t seqnum) { return seqnum >> 1; }
constexpr auto extract_dir(seqnum_t seqnum) { return usbip_dir(seqnum & 1); }
constexpr bool is_valid_seqnum(seqnum_t seqnum) { return extract_num(seqnum); }

constexpr UINT32 make_devid(UINT16 busnum, UINT16 devnum)
{
        return (busnum << 16) | devnum;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create_device_ctx_ext(_Outptr_ device_ctx_ext* &d, _In_ const vhci::ioctl_plugin &r);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void free(_In_ device_ctx_ext *ext);

_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
inline auto get_device(_In_ device_ctx_ext *ext)
{
        NT_ASSERT(ext);
        auto ctx = ext->ctx;
        return static_cast<UDECXUSBDEVICE>(ctx ? WdfObjectContextGetObject(ctx) : WDF_NO_HANDLE);
}

inline auto ptr04x(const void *ptr) // use format "%04x"
{
        auto n = reinterpret_cast<uintptr_t>(ptr);
        return static_cast<UINT32>(n);
}

/*
 * Use format "%#Ix"
 * @see make_pipe_handle 
 */ 
inline auto ph4log(USBD_PIPE_HANDLE handle)
{
        return reinterpret_cast<uintptr_t>(handle);
}

} // namespace usbip
