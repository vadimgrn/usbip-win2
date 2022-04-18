#include <ntddk.h>
#include "vhci.h"
#include "trace.h"
#include "vhci.tmh"

#include "pnp.h"
#include "irp.h"
#include "vhub.h"

#include <ntstrsafe.h>
#include <wsk.h>

GLOBALS Globals;

namespace
{

enum { WSK_REGISTER = 1, WSK_CAPTURE_PROVIDER = 2, REGISTRY_PATH = 4 }; // bits
unsigned int init_flags;

const WSK_CLIENT_DISPATCH WskDispatch{ MAKE_WSK_VERSION(1, 0) };
WSK_REGISTRATION WskRegistration;

_Use_decl_annotations_
ULONG NTAPI ProviderNpiInit(_Inout_ PRTL_RUN_ONCE, _Inout_opt_ PVOID Parameter, [[maybe_unused]] _Inout_opt_ PVOID *Context)
{
        NT_ASSERT(!Context);
        auto ProviderNpi = static_cast<WSK_PROVIDER_NPI*>(Parameter);

        auto err = WskCaptureProviderNPI(&WskRegistration, WSK_NO_WAIT, ProviderNpi);
        if (!err) {
                init_flags |= WSK_CAPTURE_PROVIDER;
                auto ver = ProviderNpi->Dispatch->Version;
                Trace(TRACE_LEVEL_INFORMATION, "NPI v.%d.%d", WSK_MAJOR_VERSION(ver), WSK_MINOR_VERSION(ver));
                return true;
        }

        Trace(TRACE_LEVEL_ERROR, "WskCaptureProviderNPI %!STATUS!", err);

        if (err == STATUS_NOINTERFACE) { // client's requested version is not supported
                WSK_PROVIDER_CHARACTERISTICS r;
                if (NT_SUCCESS(WskQueryProviderCharacteristics(&WskRegistration, &r))) {
                        Trace(TRACE_LEVEL_INFORMATION, "Supported NPI v.%d.%d-%d.%d",
                                WSK_MAJOR_VERSION(r.LowestVersion), WSK_MINOR_VERSION(r.LowestVersion),
                                WSK_MAJOR_VERSION(r.HighestVersion), WSK_MINOR_VERSION(r.HighestVersion));
                }
        }

        return false;
}

PAGEABLE NTSTATUS vhci_create(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PAGED_CODE();

	auto vdev = to_vdev(devobj);

	if (vdev->PnPState == pnp_state::Removed) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: no such device", vdev->type);
		return CompleteRequest(Irp, STATUS_NO_SUCH_DEVICE);
	}

	TraceCall("%!vdev_type_t!: irql !%!irql!", vdev->type, KeGetCurrentIrql());

	Irp->IoStatus.Information = 0;
	return CompleteRequest(Irp);
}

PAGEABLE NTSTATUS vhci_close(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PAGED_CODE();

	auto vdev = to_vdev(devobj);

	if (vdev->PnPState == pnp_state::Removed) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: no such device", vdev->type);
		return CompleteRequest(Irp, STATUS_NO_SUCH_DEVICE);
	}

	TraceCall("%!vdev_type_t!: irql !%!irql!", vdev->type, KeGetCurrentIrql());

	Irp->IoStatus.Information = 0;
	return CompleteRequest(Irp);
}

PAGEABLE void DriverUnload(__in DRIVER_OBJECT *drvobj)
{
	PAGED_CODE();

	TraceCall("%p", drvobj);
        // NT_ASSERT(!drvobj->DeviceObject);

        if (init_flags & WSK_CAPTURE_PROVIDER) {
                WskReleaseProviderNPI(&WskRegistration);
        }

        if (init_flags & WSK_REGISTER) {
                WskDeregister(&WskRegistration);
        }

        if (init_flags & REGISTRY_PATH) {
                NT_ASSERT(Globals.RegistryPath.Buffer);
	        ExFreePoolWithTag(Globals.RegistryPath.Buffer, USBIP_VHCI_POOL_TAG);
        }

        WPP_CLEANUP(drvobj);
}

/*
* Set only if such value does not exist.
*/
PAGEABLE NTSTATUS set_verbose_on(HANDLE h)
{
	PAGED_CODE();

	UNICODE_STRING name;
	RtlInitUnicodeString(&name, L"VerboseOn");

	ULONG len = 0;
	KEY_VALUE_PARTIAL_INFORMATION info;

	NTSTATUS st = ZwQueryValueKey(h, &name, KeyValuePartialInformation, &info, sizeof(info), &len);

	if (st == STATUS_OBJECT_NAME_NOT_FOUND) {
		DWORD val = 1;
		st = ZwSetValueKey(h, &name, 0, REG_DWORD, &val, sizeof(val));
	} else {
		NT_ASSERT(!st);
	}

	return st;
}

/*
* Configure Inflight Trace Recorder (IFR) parameter "VerboseOn".
* The default setting of zero causes the IFR to log errors, warnings, and informational events.
* Set to one to add verbose output to the log.
*
* reg add "HKLM\SYSTEM\ControlSet001\Services\usbip_vhci\Parameters" /v VerboseOn /t REG_DWORD /d 1 /f
*/
PAGEABLE NTSTATUS set_ifr_verbose(const UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	UNICODE_STRING params;
	RtlInitUnicodeString(&params, L"\\Parameters");

	UNICODE_STRING path;
	path.Length = 0;
	path.MaximumLength = RegistryPath->Length + params.Length;
	path.Buffer = (PWCH)ExAllocatePoolWithTag(PagedPool, path.MaximumLength + sizeof(*path.Buffer), USBIP_VHCI_POOL_TAG);

	if (!path.Buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	NTSTATUS err = RtlUnicodeStringCopy(&path, RegistryPath);
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

PAGEABLE auto save_registry_path(const UNICODE_STRING *RegistryPath)
{
        PAGED_CODE();
        
        auto &path = Globals.RegistryPath;
	USHORT max_len = RegistryPath->Length + sizeof(UNICODE_NULL);

	path.Buffer = (PWCH)ExAllocatePoolWithTag(NonPagedPool, max_len, USBIP_VHCI_POOL_TAG);
	if (!path.Buffer) {
		Trace(TRACE_LEVEL_CRITICAL, "Can't allocate %hu bytes", max_len);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

        init_flags |= REGISTRY_PATH;
        
        NT_ASSERT(!path.Length);
	path.MaximumLength = max_len;

	RtlCopyUnicodeString(&path, RegistryPath);
	TraceCall("%!USTR!", &path);

        return STATUS_SUCCESS;
}

PAGEABLE auto init_wsk()
{
        PAGED_CODE();

        WSK_CLIENT_NPI npi{ nullptr, &WskDispatch };

        if (auto err = WskRegister(&npi, &WskRegistration)) {
                Trace(TRACE_LEVEL_CRITICAL, "WskRegister: %!STATUS!", err);
                return err;
        }

        init_flags |= WSK_REGISTER;
        return STATUS_SUCCESS;
}

} // namespace


WSK_PROVIDER_NPI *GetProviderNPI()
{
        static RTL_RUN_ONCE once = RTL_RUN_ONCE_INIT;
        static WSK_PROVIDER_NPI provider;

        if (auto err = RtlRunOnceExecuteOnce(&once, ProviderNpiInit, &provider, nullptr)) {
                Trace(TRACE_LEVEL_ERROR, "RtlRunOnceExecuteOnce %!STATUS!", err);
                return nullptr;
        }

        return &provider;
}

extern "C" {

PAGEABLE DRIVER_ADD_DEVICE vhci_add_device;
	
PAGEABLE __drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH vhci_ioctl;
PAGEABLE __drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL) DRIVER_DISPATCH vhci_internal_ioctl;
PAGEABLE __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH vhci_pnp;
PAGEABLE __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL) DRIVER_DISPATCH vhci_system_control;
	 __drv_dispatchType(IRP_MJ_POWER) DRIVER_DISPATCH vhci_power;

PAGEABLE NTSTATUS DriverEntry(__in DRIVER_OBJECT *drvobj, __in UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	NTSTATUS st = set_ifr_verbose(RegistryPath);
	WPP_INIT_TRACING(drvobj, RegistryPath);
	if (st) {
		Trace(TRACE_LEVEL_CRITICAL, "Can't set IFR parameter: %!STATUS!", st);
                DriverUnload(drvobj);
                return st;
	}

	TraceCall("%p", drvobj);

        if (auto err = init_wsk()) {
                DriverUnload(drvobj);
                return err;
        }
        
        if (auto err = save_registry_path(RegistryPath)) {
                DriverUnload(drvobj);
                return err;
	}

	drvobj->MajorFunction[IRP_MJ_CREATE] = vhci_create;
	drvobj->MajorFunction[IRP_MJ_CLOSE] = vhci_close;
	drvobj->MajorFunction[IRP_MJ_PNP] = vhci_pnp;
	drvobj->MajorFunction[IRP_MJ_POWER] = vhci_power;
	drvobj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = vhci_ioctl;
	drvobj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = vhci_internal_ioctl;
	drvobj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = vhci_system_control;

	drvobj->DriverUnload = DriverUnload;
	drvobj->DriverExtension->AddDevice = vhci_add_device;

	return STATUS_SUCCESS;
}

} // extern "C"
