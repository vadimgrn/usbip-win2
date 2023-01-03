/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

namespace usbip
{

/*
 * Common for hub and device filters. 
 */
struct filter_ext
{
	DEVICE_OBJECT *self; // back pointer to the Filter Device Object for which this is the extension
//	DEVICE_OBJECT *pdo; // the second argument of DRIVER_ADD_DEVICE
	DEVICE_OBJECT *lower; // the result of IoAttachDeviceToDeviceStack(self, pdo)

	IO_REMOVE_LOCK remove_lock;
	
	// device only
	IO_REMOVE_LOCK *parent_remove_lock; // -> hub filter_ext.remove_lock

	// hub only
	DEVICE_RELATIONS *previous;
};

inline auto get_filter_ext(_In_ DEVICE_OBJECT *devobj)
{ 
	NT_ASSERT(devobj);
	return static_cast<filter_ext*>(devobj->DeviceExtension); 
}

inline auto is_hub(_In_ const filter_ext &f)
{
	return !f.parent_remove_lock;
}

inline bool is_device(_In_ const filter_ext &f)
{
	return f.parent_remove_lock;
}

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(__yes))
PAGED NTSTATUS do_add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo, _In_opt_ filter_ext *parent);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void destroy(_Inout_ filter_ext &f);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void* GetDeviceProperty(
	_In_ DEVICE_OBJECT *devobj, _In_ DEVICE_REGISTRY_PROPERTY prop, 
	_Inout_ NTSTATUS &error, _Inout_ ULONG &ResultLength);

} // namespace usbip
