/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/codeseg.h>
#include <usbip/vhci.h>

#include <usb.h>

extern "C" {
  #include <usbdlib.h>
}

namespace usbip
{

/*
 * Device extension for all FiDOs attached by ude_filter across three hierarchy levels:
 * 1. Root Hub FiDO (is_hub()): parent == nullptr (attached to USB\ROOT_HUB30).
 * 2. Top-level USB Device FiDO (is_device()): parent->is_hub() (attached to USB\VID_... child PDOs
 *    of the root hub). Owns USBD_HANDLE, queries port isolation from VHCI, hooks
 *    USB_BUS_INTERFACE_USBDI_GUID, and performs URB rewriting/post-processing.
 * 3. Child/Grandchild Function FiDO (is_function()): parent && !parent->is_hub() (attached to
 *    \Driver\usbccgp MI_xx and \Driver\HidUsb HID\... PDOs). Inherits port/isolation from
 *    parent->device and enforces IRP_MJ_CREATE, DEVPKEY_Device_SessionId, and set_device_dacl
 *    without calling USBD_CreateHandle or intercepting URBs a second time.
 */
struct filter_ext
{
        DEVICE_OBJECT *self; // back pointer to the Filter Device Object for which this is the extension
        DEVICE_OBJECT *target; // = IoAttachDeviceToDeviceStack(self, pdo); next lower object in the device stack
        DEVICE_OBJECT *pdo; // the second argument of DRIVER_ADD_DEVICE

        IO_REMOVE_LOCK remove_lock;
        filter_ext *parent; // nullptr for root hub, hub for USB device, USB device or function for usbccgp/HidUsb child PDO
        DEVICE_RELATIONS *previous; // children (for root hub, composite parent device, or HidUsb function)

        union {
                struct {
                        DEVICE_OBJECT *vhci; // UDE host controller, must be dereferenced
                } hub; // is_hub()

                struct {
                        USBD_HANDLE usbd_handle; // valid only for is_device()
                        vhci::isolation_data iso;
                        ULONG port;
                } device; // !is_hub() (both is_device() and is_function())
        };

        constexpr bool is_hub() const { return !parent; }
        constexpr bool is_device() const { return parent && parent->is_hub(); } // top-level USB device on root hub only
        constexpr bool is_function() const { return parent && !parent->is_hub(); } // usbccgp MI_xx or HidUsb HID\... child PDO
};

inline auto get_filter_ext(_In_ DEVICE_OBJECT *devobj)
{ 
	NT_ASSERT(devobj);
	return static_cast<filter_ext*>(devobj->DeviceExtension); 
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED DEVICE_RELATIONS* clone_relations(_In_ const DEVICE_RELATIONS &src);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED DEVICE_RELATIONS* clone_relations_retry(
        _In_ const DEVICE_RELATIONS &src, _In_ ULONG max_attempts = 3, _In_ LONGLONG delay_ms = 50);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void destroy_relations(_Inout_ DEVICE_RELATIONS* &relations);

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS do_add_device(
        _In_ DRIVER_OBJECT *drvobj, 
        _In_ DEVICE_OBJECT *pdo, 
        _In_opt_ filter_ext *parent,
        _Out_opt_ filter_ext **out_fltr = nullptr);


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_filterable_child_driver(_In_opt_ const DRIVER_OBJECT *driver);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS query_port_isolation(
        _Inout_ filter_ext &hub, _In_ int port, _In_ DEVICE_OBJECT *pdo, _Out_ vhci::isolation_data &data);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void destroy(_Inout_ filter_ext &f);

} // namespace usbip
