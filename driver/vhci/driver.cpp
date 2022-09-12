#include "driver.h"
#include "device.h"
#include "trace.h"
#include "driver.tmh"

#include <ntstrsafe.h>

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGEABLE void DriverUnload(_In_ DRIVER_OBJECT *drvobj)
{
	PAGED_CODE();
	TraceMsg("%04x", ptr4log(drvobj));
	WPP_CLEANUP(drvobj);
}

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGEABLE void CleanupCallback(_In_ WDFOBJECT DriverObject)
{
	PAGED_CODE();
	if (auto drvobj = WdfDriverWdmGetDriverObject(static_cast<WDFDRIVER>(DriverObject))) {
		DriverUnload(drvobj);
	}
}

/*
 * Set only if such value does not exist.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGEABLE NTSTATUS set_verbose_on(HANDLE h)
{
	PAGED_CODE();

	UNICODE_STRING name;
	RtlInitUnicodeString(&name, L"VerboseOn");

	ULONG len = 0;
	KEY_VALUE_PARTIAL_INFORMATION info;

	auto err = ZwQueryValueKey(h, &name, KeyValuePartialInformation, &info, sizeof(info), &len);

	if (err == STATUS_OBJECT_NAME_NOT_FOUND) {
		DWORD val = 1;
		err = ZwSetValueKey(h, &name, 0, REG_DWORD, &val, sizeof(val));
	} else {
		NT_ASSERT(!err);
	}

	return err;
}

/*
 * Configure Inflight Trace Recorder (IFR) parameter "VerboseOn".
 * The default setting of zero causes the IFR to log errors, warnings, and informational events.
 * Set to one to add verbose output to the log.
 *
 * reg add "HKLM\SYSTEM\ControlSet001\Services\usbip_vhci\Parameters" /v VerboseOn /t REG_DWORD /d 1 /f
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGEABLE NTSTATUS set_ifr_verbose(const UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	DECLARE_CONST_UNICODE_STRING(params, L"\\Parameters");

	UNICODE_STRING path;
	path.Length = 0;
	path.MaximumLength = RegistryPath->Length + params.Length;
	path.Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_PAGED|POOL_FLAG_UNINITIALIZED, path.MaximumLength + sizeof(*path.Buffer), USBIP_VHCI_POOL_TAG);

	if (!path.Buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto err = RtlUnicodeStringCopy(&path, RegistryPath);
	NT_ASSERT(!err);

	err = RtlUnicodeStringCat(&path, &params);
	NT_ASSERT(!err);

	OBJECT_ATTRIBUTES attrs;
	InitializeObjectAttributes(&attrs, &path, OBJ_KERNEL_HANDLE, nullptr, nullptr);

	HANDLE h = nullptr;
	err = ZwCreateKey(&h, KEY_WRITE, &attrs, 0, nullptr, 0, nullptr);
	if (!err) {
		err = set_verbose_on(h);
		ZwClose(h);
	}

	ExFreePoolWithTag(path.Buffer, USBIP_VHCI_POOL_TAG);
	return err;
}

} // namespace


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
__declspec(code_seg("INIT"))
EXTERN_C NTSTATUS DriverEntry(_In_ DRIVER_OBJECT *DriverObject, _In_ UNICODE_STRING *RegistryPath)
{
        PAGED_CODE();

	auto st = set_ifr_verbose(RegistryPath);
	WPP_INIT_TRACING(DriverObject, RegistryPath);
	if (st) {
		Trace(TRACE_LEVEL_CRITICAL, "set_ifr_verbose %!STATUS!", st);
		DriverUnload(DriverObject);
		return st;
	}

        TraceMsg("DriverObject %04x, RegistryPath %!USTR!", ptr4log(DriverObject), RegistryPath);

	WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT(&attrs);

	attrs.EvtCleanupCallback = CleanupCallback;

        WDF_DRIVER_CONFIG cfg;
        WDF_DRIVER_CONFIG_INIT(&cfg, DriverDeviceAdd);
        cfg.DriverPoolTag = USBIP_VHCI_POOL_TAG;

        if (auto err = WdfDriverCreate(DriverObject, RegistryPath, &attrs, &cfg, WDF_NO_HANDLE)) {
                Trace(TRACE_LEVEL_CRITICAL,"WdfDriverCreate %!STATUS!", err);
                DriverUnload(DriverObject);
		return err;
        }

        return STATUS_SUCCESS;
}
