/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv\ch9.h>
#include <libdrv\wdf_cpp.h>

#include <usbip\proto.h>

#include <wdfusb.h>
#include <UdeCx.h>

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
 * Context space for WDFDEVICE, Virtual Host Controller Interface.
 * The parent is WDFDRIVER.
 */
struct vhci_ctx
{
        int usb2_ports; // constant

        int devices_cnt; // constant, usb2_ports + usb3 ports
        UDECXUSBDEVICE *devices; // do not access directly, functions must be used
        WDFSPINLOCK devices_lock;

        LIST_ENTRY fileobjects; // @see fileobject_ctx::entry
        WDFQUEUE reads; // IRP_MJ_READ
        int events_subscribers; // SUM(fileobject_ctx::process_events)
        WDFWAITLOCK events_lock;

        WDFIOTARGET target_self;

        unsigned int reattach_max_attempts; // constants
        unsigned int reattach_first_delay;
        unsigned int reattach_max_delay;

        LIST_ENTRY reattach_requests; // @see attach_ctx::entry
        WDFSPINLOCK reattach_requests_lock;

        LONG removing; // use set_flag/get_flag
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(vhci_ctx, get_vhci_ctx)

inline auto get_handle(_In_ vhci_ctx *ctx)
{
        NT_ASSERT(ctx);
        return static_cast<WDFDEVICE>(WdfObjectContextGetObject(ctx));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_valid_port(_In_ const vhci_ctx &ctx, _In_ int port);

struct wsk_context;
struct device_ctx;

struct device_attributes
{
        // from ioctl::plugin_hardware, .Buffer-s are allocated in PagedPool
        UNICODE_STRING node_name;
        UNICODE_STRING service_name;
        UNICODE_STRING busid;
        //
        vhci::imported_device_properties properties; // for ioctl::get_imported_devices
};

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

        device_attributes attr;
        bool ep0_added;

        auto node_name() { return &attr.node_name; }
        auto service_name() { return &attr.service_name; }
        auto busid() { return &attr.busid; }

        auto& properties() { return attr.properties; }
};

/*
 * @param mem must contain device_ctx_ext
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_device_ctx_ext(_In_ WDFMEMORY mem)
{
        return *static_cast<device_ctx_ext*>(WdfMemoryGetBuffer(mem, nullptr));
}

/*
 * Context space for UDECXUSBDEVICE - emulated USB device.
 */
struct device_ctx
{
        WDFMEMORY ctx_ext;
        auto& ext() const { return get_device_ctx_ext(ctx_ext); }

        auto sock() const { return ext().sock; }
        auto& attributes() const { return ext().attr; }

        auto speed() const { return ext().properties().speed; }
        auto devid() const { return ext().properties().devid; }

        WDFDEVICE vhci; // parent, virtual (emulated) host controller interface

        UDECXUSBENDPOINT ep0; // default control pipe
        WDFSPINLOCK endpoint_list_lock; // for endpoint_ctx::entry

        WDFSPINLOCK send_lock; // for WskSend on sock()

        int port; // vhci_ctx.devices[port - 1]
        seqnum_t seqnum; // @see next_seqnum

        LONG unplugged; // initiated detach that may still be ongoing, use set_flag/get_flag

        LIST_ENTRY requests; // list head, requests that are waiting for USBIP_RET_SUBMIT from a server
        WDFSPINLOCK requests_lock;

        // statistics
        UINT64 sent_requests; // were sent successfully
        UINT64 cancelable_requests; // marked as

        _KTHREAD *recv_thread;
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(device_ctx, get_device_ctx)

inline auto get_handle(_In_ device_ctx *ctx)
{
        NT_ASSERT(ctx);
        return static_cast<UDECXUSBDEVICE>(WdfObjectContextGetObject(ctx));
}

/*
 * Context space for UDECXUSBENDPOINT.
 */
struct endpoint_ctx
{
        UDECXUSBDEVICE device; // parent
        WDFQUEUE queue; // child
        USB_ENDPOINT_DESCRIPTOR_AUDIO descriptor;

        CCHAR priority_boost; 
        static_assert(!IO_NO_INCREMENT);

        // UCHAR interface_number; // interface to which it belongs
        // UCHAR alternate_setting;

        USBD_PIPE_HANDLE PipeHandle;
        LIST_ENTRY entry; // list head if default control pipe, protected by device_ctx::endpoint_list_lock
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(endpoint_ctx, get_endpoint_ctx)

WDF_DECLARE_CONTEXT_TYPE(UDECXUSBENDPOINT); // WdfObjectGet_UDECXUSBENDPOINT
inline auto& get_endpoint(_In_ WDFQUEUE queue)
{
        return *WdfObjectGet_UDECXUSBENDPOINT(queue);
}


/*
 * Context space for WDFREQUEST.
 */
struct request_ctx
{
        LIST_ENTRY entry; // head is device_ctx::requests
        UDECXUSBENDPOINT endpoint;
        seqnum_t seqnum;
        bool cancelable;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(request_ctx, get_request_ctx)

inline auto get_handle(_In_ request_ctx *ctx)
{
        NT_ASSERT(ctx);
        return static_cast<WDFREQUEST>(WdfObjectContextGetObject(ctx));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
UDECXUSBDEVICE get_device(_In_ WDFREQUEST Request);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto get_vhci(_In_ WDFREQUEST Request)
{
        auto queue = WdfRequestGetIoQueue(Request);
        return WdfIoQueueGetDevice(queue);
}


/*
 * Context space for WDFFILEOBJECT.
 * @see WdfFileObjectGetDevice
 */
struct fileobject_ctx
{
        LIST_ENTRY entry; // head is vhci_ctx::fileobjects
        WDFCOLLECTION events; // WDFMEMORY(device_state) that are waiting for IRP_MJ_READ
        bool process_events; // if IRP_MJ_READ was issued, see vhci_ctx::events_subscribers
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(fileobject_ctx, get_fileobject_ctx)

inline auto get_handle(_In_ fileobject_ctx *ctx)
{
        NT_ASSERT(ctx);
        return static_cast<WDFFILEOBJECT>(WdfObjectContextGetObject(ctx));
}


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
seqnum_t next_seqnum(_Inout_ device_ctx &dev, _In_ bool dir_in);

constexpr auto extract_num(seqnum_t seqnum) { return seqnum >> 1; }
constexpr auto extract_dir(seqnum_t seqnum) { return usbip::direction(seqnum & 1); }
constexpr bool is_valid_seqnum(seqnum_t seqnum) { return extract_num(seqnum); }

constexpr UINT32 make_devid(UINT16 busnum, UINT16 devnum)
{
        return (busnum << 16) | devnum;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_device_ctx_ext(_Inout_ WDFMEMORY &ctx_ext, _In_ WDFOBJECT parent, _In_ const vhci::ioctl::plugin_hardware &r);

inline bool set_flag(_Inout_ LONG &target)
{
        return InterlockedExchange(&target, true);
}

inline bool get_flag(_Inout_ LONG &target)
{
        return InterlockedCompareExchange(&target, false, false);
}

} // namespace usbip
