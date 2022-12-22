/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "driver.h"
#include <libdrv\codeseg.h>
#include "trace.h"
#include "driver.tmh"

#include <libdrv\wdf_cpp.h>
#include <libdrv\dbgcommon.h>

namespace
{

using namespace usbip;

struct filter_ctx
{
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(filter_ctx, get_filter_ctx)

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void driver_cleanup(_In_ WDFOBJECT Object)
{
	PAGED_CODE();

	auto drv = static_cast<WDFDRIVER>(Object);
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(drv));

	auto drvobj = WdfDriverWdmGetDriverObject(drv);
	WPP_CLEANUP(drvobj);
}

_Function_class_(EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI internal_device_control(
	_In_ WDFQUEUE queue,
	_In_ WDFREQUEST request, 
	_In_ size_t OutputBufferLength, 
	_In_ size_t InputBufferLength, 
	_In_ ULONG IoControlCode)
{
	TraceDbg("OutputBufferLength %Iu, InputBufferLength %Iu, %s(%#lx)", 
		  OutputBufferLength, InputBufferLength, internal_device_control_name(IoControlCode), IoControlCode);

	auto dev = WdfIoQueueGetDevice(queue);
	// auto ctx = get_filter_ctx(dev);

	WDF_REQUEST_SEND_OPTIONS opts;
	WDF_REQUEST_SEND_OPTIONS_INIT(&opts, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

	if (auto target = WdfDeviceGetIoTarget(dev); !WdfRequestSend(request, target, &opts)) {
		auto st = WdfRequestGetStatus(request);
		Trace(TRACE_LEVEL_ERROR, "WdfRequestSend error, request %!STATUS!", st);
		WdfRequestComplete(request, st);
	}
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto NTAPI queue_create(_In_ WDFDEVICE dev)
{
	PAGED_CODE();

	WDF_IO_QUEUE_CONFIG cfg;
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchParallel);
	cfg.EvtIoInternalDeviceControl = internal_device_control;

	WDFQUEUE queue;
	if (auto err = WdfIoQueueCreate(dev, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &queue)) {
		Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
		return err;
	}

	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(queue));
	return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto is_root_hub30(_In_ WDFDEVICE_INIT *init)
{
	PAGED_CODE();

	WDFMEMORY mem;
	if (auto err = WdfFdoInitAllocAndQueryProperty(init, DevicePropertyHardwareID, 
		                                       PagedPool, WDF_NO_OBJECT_ATTRIBUTES, &mem)) {
		Trace(TRACE_LEVEL_ERROR, "WdfFdoInitAllocAndQueryProperty(HardwareID) %!STATUS!", err);
		return false;
	}

	size_t buf_sz;
	auto buf = (WCHAR*)WdfMemoryGetBuffer(mem, &buf_sz);

	const UNICODE_STRING hwid { 
		.Length = USHORT(buf_sz) - 2*sizeof(WCHAR), // REG_MULTI_SZ
		.MaximumLength = USHORT(buf_sz), 
		.Buffer = buf
	};

	TraceDbg("HardwareID='%!USTR!'", &hwid);

	DECLARE_CONST_UNICODE_STRING(root_hub30, L"USB\\ROOT_HUB30"); // FIXME: find constant in headers
	bool equal = RtlEqualUnicodeString(&hwid, &root_hub30, true);

	WdfObjectDelete(mem);
	return equal;
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED bool is_above_usbip2_vhci(_In_ WDFDEVICE_INIT *init)
{
	PAGED_CODE();

	auto pdo = WdfFdoInitWdmGetPhysicalDevice(init);
	if (!pdo) {
		Trace(TRACE_LEVEL_ERROR, "WdfFdoInitWdmGetPhysicalDevice error");
		return false;
	}

	auto drv = pdo->DriverObject;
	auto name = &drv->DriverName;

	TraceDbg("DriverName '%!USTR!'", name);

	DECLARE_CONST_UNICODE_STRING(vhci, L"\\Driver\\usbip2_vhci"); // FIXME: declare in header?
	return RtlEqualUnicodeString(name, &vhci, true);
}

_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED NTSTATUS NTAPI device_add(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *init)
{
	PAGED_CODE();

	if (!(is_root_hub30(init) && is_above_usbip2_vhci(init))) {
		TraceDbg("Skip this device");
		return STATUS_SUCCESS;
	}

	WdfFdoInitSetFilter(init);
	WdfDeviceInitSetDeviceType(init, FILE_DEVICE_UNKNOWN); // FILE_DEVICE_USBEX

	WDF_OBJECT_ATTRIBUTES attrs;
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, filter_ctx);

	WDFDEVICE dev;
	if (auto err = WdfDeviceCreate(&init, &attrs, &dev)) {
		Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreate %!STATUS!", err);
		return err;
	}

	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(dev));
	return queue_create(dev);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT auto driver_create(_In_ DRIVER_OBJECT *DriverObject, _In_ UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	WDF_OBJECT_ATTRIBUTES attrs;
	WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
	attrs.EvtCleanupCallback = driver_cleanup;

	WDF_DRIVER_CONFIG cfg;
	WDF_DRIVER_CONFIG_INIT(&cfg, device_add);
	cfg.DriverPoolTag = POOL_TAG;

	return WdfDriverCreate(DriverObject, RegistryPath, &attrs, &cfg, nullptr);
}

} // namespace


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT EXTERN_C NTSTATUS DriverEntry(_In_ DRIVER_OBJECT *DriverObject, _In_ UNICODE_STRING *RegistryPath)
{
	if (auto err = driver_create(DriverObject, RegistryPath)) {
		return err;
	}

	WPP_INIT_TRACING(DriverObject, RegistryPath);
	Trace(TRACE_LEVEL_INFORMATION, "RegistryPath '%!USTR!'", RegistryPath);

	return STATUS_SUCCESS;
}
