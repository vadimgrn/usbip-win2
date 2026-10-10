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
 * Common for hub and device filters.
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
                        USBD_HANDLE usbd_handle;
                        vhci::isolation_data iso;
                        ULONG port;
                } device; // !is_hub()
        };

        constexpr bool is_hub() const noexcept { return !parent; }
        constexpr bool is_device() const noexcept { return parent && parent->is_hub(); }
        constexpr bool is_function() const noexcept { return parent && !parent->is_hub(); }
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
