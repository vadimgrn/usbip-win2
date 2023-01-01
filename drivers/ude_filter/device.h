/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

namespace usbip
{

struct filter_ext
{
	DEVICE_OBJECT *self; // back pointer to the Filter Device Object for which this is the extension
//	DEVICE_OBJECT *pdo; // the second argument of DRIVER_ADD_DEVICE
	DEVICE_OBJECT *lower; // the result of IoAttachDeviceToDeviceStack(self, pdo)
};


inline auto get_filter_ext(_In_ DEVICE_OBJECT *devobj)
{ 
	NT_ASSERT(devobj);
	return static_cast<filter_ext*>(devobj->DeviceExtension); 
}

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(__yes))
PAGED NTSTATUS add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo);

_Function_class_(DRIVER_DISPATCH)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
NTSTATUS dispatch_lower(_In_ DEVICE_OBJECT *devobj, _Inout_ IRP *irp);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void destroy(_Inout_ filter_ext &fltr);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void* GetDeviceProperty(
	_In_ DEVICE_OBJECT *devobj, _In_ DEVICE_REGISTRY_PROPERTY prop, 
	_Inout_ NTSTATUS &error, _Inout_ ULONG &ResultLength);

} // namespace usbip
