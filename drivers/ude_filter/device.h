/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

namespace usbip
{

struct device_ext
{
	DEVICE_OBJECT *self; // @see get_device_ext
	DEVICE_OBJECT *lower;
};


inline auto& get_device_ext(_In_ DEVICE_OBJECT *devobj)
{ 
	NT_ASSERT(devobj);
	return *static_cast<device_ext*>(devobj->DeviceExtension); 
}

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(__yes))
PAGED NTSTATUS add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void* GetDeviceProperty(
	_In_ DEVICE_OBJECT *devobj, _In_ DEVICE_REGISTRY_PROPERTY prop, 
	_Inout_ NTSTATUS &error, _Inout_ ULONG &ResultLength);

} // namespace usbip
