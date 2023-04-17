/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

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

	union {
		struct {
			DEVICE_RELATIONS *previous; // children
		} hub; // is_hub == true

		struct {
			IO_REMOVE_LOCK *parent_remove_lock; // -> hub filter_ext.remove_lock
			USBD_HANDLE usbd_handle;
		} device; // is_hub == false
	};
	bool is_hub;
};

inline auto get_filter_ext(_In_ DEVICE_OBJECT *devobj)
{ 
	NT_ASSERT(devobj);
	return static_cast<filter_ext*>(devobj->DeviceExtension); 
}

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS do_add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo, _In_opt_ filter_ext *parent);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void destroy(_Inout_ filter_ext &f);

} // namespace usbip
