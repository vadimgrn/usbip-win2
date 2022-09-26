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

struct vhci_ctx
{
        WDFQUEUE queue;

        UDECXUSBDEVICE devices[vhci::TOTAL_PORTS]; // do not access directly, functions must be used
        WDFSPINLOCK devices_lock;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(vhci_ctx, get_vhci_ctx)

struct device_ctx;

struct device_ctx_data
{
        device_ctx *ctx;
        
        usb_device_speed speed;

        UINT16 vendor_id;
        UINT16 product_id;

        UINT32 devid;
        static_assert(sizeof(devid) == sizeof(usbip_header_basic::devid));

        seqnum_t seqnum; // @see next_seqnum
        wsk::SOCKET *sock;

        // from vhci::ioctl_plugin
        PSTR busid;
        UNICODE_STRING node_name;
        UNICODE_STRING service_name;
        UNICODE_STRING serial; // user-defined
        //
};

struct device_ctx
{
        WDFDEVICE vhci;
        bool destroyed;

        int port; // vhci_ctx.devices[port - 1], unique device id, this is not roothub's port number
        device_ctx_data *data;
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(device_ctx, get_device_ctx)

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


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create(_Outptr_ device_ctx_data* &d, _In_ const vhci::ioctl_plugin &r);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void free(_In_ device_ctx_data *data);


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

constexpr UINT32 make_devid(UINT16 busnum, UINT16 devnum)
{
        return (busnum << 16) | devnum;
}

} // namespace usbip
