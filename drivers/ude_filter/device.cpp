/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>
#include <initguid.h>

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "driver.h"

#include <usbip/consts.h>
#include <libdrv/timeout.h>
#include <libusbip/generic_handle_ex.h>
#include <resources/messages.h>
#include <ntstrsafe.h>

using namespace usbip;
using namespace libdrv;

namespace
{

struct device_interfaces_traits
{
        static PZZWSTR invalid() { return nullptr; }
};

/*
 * IoGetDeviceInterfaces allocates a multi-string buffer from PagedPool.
 * It must be freed using ExFreePool at IRQL <= APC_LEVEL (PASSIVE_LEVEL).
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline void close_handle(_In_ PZZWSTR ptr, _In_ device_interfaces_traits)
{
        PAGED_CODE();
        ExFreePool(ptr);
}

using device_interfaces_handle = generic_handle<device_interfaces_traits>;

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto init(_Inout_ filter_ext &f, _In_opt_ filter_ext *parent)
{
	PAGED_CODE();

	IoInitializeRemoveLock(&f.remove_lock, unique_ptr::pooltag, 0, 0);

	if (!parent) {
		NT_ASSERT(f.is_hub);
	} else if (auto err = IoAcquireRemoveLock(&parent->remove_lock, f.self)) {
		Trace(TRACE_LEVEL_ERROR, "IoAcquireRemoveLock %!STATUS!", err);
		return err;
	} else {
		f.device.parent = parent;
	}

	return STATUS_SUCCESS;
}

/*
 * filter_ext must be zero-initialized, IoCreateDevice guarantees that.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void do_destroy(_Inout_ filter_ext &f)
{
	PAGED_CODE();

	if (f.is_hub) {
                destroy_relations(f.hub.previous);

                if (auto &file = f.hub.vhci_file) {
                        ObDereferenceObject(file); // see get_vhci_device, IoGetDeviceObjectPointer
                        file = nullptr;
                        f.hub.vhci_device = nullptr;
                }
	} else {
		auto &dev = f.device;
		NT_ASSERT(!dev.usbd_handle); // @see IRP_MN_REMOVE_DEVICE

		if (auto parent = dev.parent) {
			IoReleaseRemoveLock(&parent->remove_lock, f.self);
		}
	}
}

/*
 * DRIVER_OBJECT.DriverName is an undocumented member and can't be used.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool driver_name_equal(
	_In_ DRIVER_OBJECT *driver, _In_ const UNICODE_STRING &expected, _In_ bool CaseInSensitive)
{
	PAGED_CODE();

	const auto buf_sz = 1024UL;
	unique_ptr buf(uninitialized, PagedPool, buf_sz);
	if (!buf) {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate %lu bytes", buf_sz);
		return false;
	}

	auto info = buf.get<OBJECT_NAME_INFORMATION>();

	ULONG actual_sz;
	if (auto err = ObQueryNameString(driver, info, buf_sz, &actual_sz)) {
		Trace(TRACE_LEVEL_ERROR, "ObQueryNameString %!STATUS!", err);
		return false;
	}

	TraceDbg("'%!USTR!'", &info->Name);
	return RtlEqualUnicodeString(&info->Name, &expected, CaseInSensitive);
}

/*
 * Do not check that HardwareID is "USB\ROOT_HUB30" because above usbip2_ude can be nothing else.
 * 
 * for (auto cur = IoGetAttachedDeviceReference(pdo); cur; ) { // @see IoGetDeviceAttachmentBaseRef
 * 	auto lower = IoGetLowerDeviceObject(cur);
 *	ObDereferenceObject(cur);
 *	cur = lower;
 * }
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_above_vhci(_In_ DEVICE_OBJECT *pdo)
{
	PAGED_CODE();

	DECLARE_CONST_UNICODE_STRING(prefix, L"\\Driver\\");

	UNICODE_STRING fname;
	RtlUnicodeStringInit(&fname, driver_filename);

	DECLARE_UNICODE_STRING_SIZE(driver_name, 64);
	NT_VERIFY(NT_SUCCESS(RtlUnicodeStringCopy(&driver_name, &prefix)));
	NT_VERIFY(NT_SUCCESS(RtlUnicodeStringCat(&driver_name, &fname)));

	return driver_name_equal(pdo->DriverObject, driver_name, true);
}
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_vhci_device(_Inout_ filter_ext &hub_fltr, _Out_ DEVICE_OBJECT* &vhci)
{
        PAGED_CODE();

        NT_ASSERT(hub_fltr.is_hub);
        auto &hub = hub_fltr.hub;

        if (auto d = hub.vhci_device) {
                vhci = d;
                return STATUS_SUCCESS;
        }

        vhci = nullptr;

        PZZWSTR list{};
        auto st = IoGetDeviceInterfaces(&vhci::GUID_DEVINTERFACE_USBIP_VHCI, nullptr, 0, &list);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoGetDeviceInterfaces %!STATUS!", st);
                return st;
        }
        device_interfaces_handle del(list);

        for (auto p = list; *p; ) {
                UNICODE_STRING link;
                RtlInitUnicodeString(&link, p);

                st = IoGetDeviceObjectPointer(&link, FILE_READ_DATA, &hub.vhci_file, &hub.vhci_device);
                if (NT_SUCCESS(st)) {
                        vhci = hub.vhci_device;
                        return STATUS_SUCCESS;
                }

                Trace(TRACE_LEVEL_WARNING, "IoGetDeviceObjectPointer('%!USTR!') %!STATUS!", &link, st);
                p += link.Length/sizeof(*link.Buffer) + 1;
        }

        return STATUS_NOT_FOUND;
}

constexpr size_t SizeOf_DEVICE_RELATIONS(ULONG cnt)
{
	return sizeof(DEVICE_RELATIONS) + (cnt > 1 ? (cnt - 1) * sizeof(PDEVICE_OBJECT) : 0);
}
static_assert(SizeOf_DEVICE_RELATIONS(0) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(1) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(2) == sizeof(DEVICE_RELATIONS) + sizeof(PDEVICE_OBJECT));

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED DEVICE_RELATIONS* usbip::clone_relations(_In_ const DEVICE_RELATIONS &src)
{
	PAGED_CODE();

	constexpr auto max_extra = (MAXULONG - sizeof(DEVICE_RELATIONS)) / sizeof(PDEVICE_OBJECT);
	if (src.Count > 1 && (src.Count - 1) > max_extra) {
		Trace(TRACE_LEVEL_ERROR, "Relations count overflow: %lu", src.Count);
		return nullptr;
	}

	auto sz = SizeOf_DEVICE_RELATIONS(src.Count);
	unique_ptr ptr(uninitialized, PagedPool, sz);

	if (ptr) {
		RtlCopyMemory(ptr.get(), &src, sz);

		for (ULONG i = 0; i < src.Count; ++i) {
			NT_ASSERT(src.Objects[i]);
			ObReferenceObject(src.Objects[i]);
		}
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", sz);
	}

	return ptr.release<DEVICE_RELATIONS>();
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED DEVICE_RELATIONS* usbip::clone_relations_retry(
	_In_ const DEVICE_RELATIONS &src, _In_ ULONG max_attempts, _In_ LONGLONG delay_ms)
{
        PAGED_CODE();

        if (!max_attempts) {
                return nullptr;
        }

        for (ULONG i = 1; ; ++i) {
                if (auto ptr = clone_relations(src)) {
                        return ptr;
                }

                if (i >= max_attempts) {
                        Trace(TRACE_LEVEL_ERROR, "clone_relations failed after %lu attempts (%lu relations)", i, src.Count);
                        return nullptr;
                }

                auto interval = wdm::make_timeout(delay_ms*wdm::msec, wdm::period::relative);
                KeDelayExecutionThread(KernelMode, false, &interval);
        }
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void usbip::destroy_relations(_Inout_ DEVICE_RELATIONS* &relations)
{
	PAGED_CODE();

	if (!relations) {
		return;
	}

	for (ULONG i = 0; i < relations->Count; ++i) {
		NT_ASSERT(relations->Objects[i]);
		ObDereferenceObject(relations->Objects[i]);
	}

        unique_ptr{relations};
        relations = nullptr;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void usbip::destroy(_Inout_ filter_ext &f)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(f.self));

	if (auto &target = f.target) {
		IoDetachDevice(target);
		target = nullptr;
	}

	do_destroy(f);
	IoDeleteDevice(f.self);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::query_port_isolation(
        _Inout_ filter_ext &hub_fltr, _In_ int port, _Out_ get_port_isolation &iso)
{
        PAGED_CODE();
        NT_ASSERT(hub_fltr.is_hub);

        DEVICE_OBJECT *vhci{};
        auto st = get_vhci_device(hub_fltr, vhci);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        iso = { .port = port };
        iso.size = sizeof(iso);

        KEVENT event;
        KeInitializeEvent(&event, NotificationEvent, false);

        IO_STATUS_BLOCK iost{};

        auto irp = IoBuildDeviceIoControlRequest(
                        vhci::ioctl::INTERNAL_GET_PORT_ISOLATION, vhci,
                        &iso, sizeof(iso),
                        &iso, sizeof(iso),
                        true, &event, &iost);

        if (!irp) {
                Trace(TRACE_LEVEL_ERROR, "IoBuildDeviceIoControlRequest failed");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        st = IoCallDriver(vhci, irp);
        if (st == STATUS_PENDING) {
                KeWaitForSingleObject(&event, Executive, KernelMode, false, nullptr);
                st = iost.Status;
        }

        if (auto ok = NT_SUCCESS(st) && iost.Information == sizeof(iso) &&
                      iso.size == sizeof(iso) && iso.port == port; !ok) {
                st = USBIP_ERROR_ABI;
        }

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "port %d, %!STATUS!", port, st);
        }

        return st;
}

/*
 * We propagate a few Flags bits, DeviceType, and Characteristics from the device object next beneath us.
 * We need to make these copies because the I/O Manager bases some of its decisions (such as MDL vs.
 * system buffer for read/write requests) on what it sees in the topmost device object.
 *
 * We don't need to copy the SectorSize or AlignmentRequirement members of the lower device object,
 * IoAttachDeviceToDeviceStack will do that automatically.
 *
 * There's ordinarily no need for a filter device object (FiDO) to have its own name. If the function
 * driver names its device object and creates a symbolic link, or registers a device interface for it,
 * an application will be able to open a handle for the device. Every IRP sent to the device gets sent
 * first to the topmost FiDO driver, whether or not that FiDO has its own name.
 *
 * FILE_DEVICE_SECURE_OPEN:
 * We explicitly enforce FILE_DEVICE_SECURE_OPEN on the FiDO. When set, the I/O Manager applies the
 * device object's security descriptor to all open requests—including relative opens and opens with
 * trailing path/file names across the device's namespace. Because ude_filter serves as the security gate
 * for session and user isolation in create (IRP_MJ_CREATE), setting this characteristic ensures
 * that the I/O Manager performs security checks across the entire namespace and dispatches every open
 * request to this driver, preventing unprivileged callers from bypassing access checks via relative or
 * namespace opens even if the underlying PDO or FDO did not set this flag.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS usbip::do_add_device(
        _In_ DRIVER_OBJECT *drvobj, 
        _In_ DEVICE_OBJECT *pdo, 
        _In_opt_ filter_ext *parent,
        _Out_opt_ filter_ext **out_fltr)
{
	PAGED_CODE();

	filter_ext *fltr{};
	DEVICE_OBJECT *fido{}; // Filter Device Object

	if (auto err = IoCreateDevice(drvobj, sizeof(*fltr), nullptr,
				      FILE_DEVICE_UNKNOWN, 0, false, &fido)) {
		Trace(TRACE_LEVEL_ERROR, "IoCreateDevice %!STATUS!", err);
		return err;
	}

	fltr = get_filter_ext(fido); // zeroed by IoCreateDevice

	fltr->self = fido;
	fltr->pdo = pdo;
	fltr->is_hub = !parent;

	if (auto err = init(*fltr, parent)) {
		destroy(*fltr);
		return err;
	}

	auto &target = fltr->target; // FDO or another FiDO

	target = IoAttachDeviceToDeviceStack(fido, pdo); // object to which fido was attached
	if (!target) {
		auto err = STATUS_NO_SUCH_DEVICE;
		Trace(TRACE_LEVEL_ERROR, "IoAttachDeviceToDeviceStack %!STATUS!", err);
		destroy(*fltr);
		return err;
	}

	fido->DeviceType = target->DeviceType;
	fido->Characteristics = target->Characteristics | FILE_DEVICE_SECURE_OPEN; 
	fido->Flags |= target->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE | DO_POWER_INRUSH);

	if (!fltr->is_hub) {
		auto &dev = fltr->device;
		if (auto err = USBD_CreateHandle(fido, target, USBD_CLIENT_CONTRACT_VERSION_602, unique_ptr::pooltag, &dev.usbd_handle)) {
			Trace(TRACE_LEVEL_ERROR, "USBD_CreateHandle %!STATUS!", err);
			destroy(*fltr);
			return err;
		}
	}

	Trace(TRACE_LEVEL_INFORMATION, "FiDO %04x, pdo %04x (DeviceType %#lx), target %04x (DeviceType %#lx)", 
		ptr04x(fido), ptr04x(pdo), pdo->DeviceType, ptr04x(target), target->DeviceType);

        if (out_fltr) {
                *out_fltr = fltr;
        }

	fido->Flags &= ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}

/*
 * If upper filter driver fails to load, all USB devices will not work.
 * To avoid this, the function always returns success.
 * If do_add_device returns an error, USBip software will not work, but other USB devices will be fine.
 */
_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS usbip::add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *hub_or_hci_pdo)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "drv %04x, pdo %04x", ptr04x(drvobj), ptr04x(hub_or_hci_pdo));

	if (!is_above_vhci(hub_or_hci_pdo)) {
		TraceDbg("Skip this device");
		return STATUS_SUCCESS;
	}

	if (auto err = do_add_device(drvobj, hub_or_hci_pdo, nullptr)) {
		Trace(TRACE_LEVEL_CRITICAL, "Failed to add a device");
	}

	return STATUS_SUCCESS;
}
