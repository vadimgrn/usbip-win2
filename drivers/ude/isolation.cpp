/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "isolation.h"
#include "trace.h"
#include "isolation.tmh"

#include "context.h"
#include "vhci.h"
#include "driver.h"

#include <libdrv/handle.h>
#include <libdrv/security.h>
#include <libdrv/strconv.h>

#include <ntddstor.h>
#include <ntddvol.h>
#include <wdmguid.h>
#include <devpkey.h>
#include <ntstrsafe.h>

using namespace usbip;
using namespace libdrv;

namespace
{

constexpr GUID guid_null{};

struct volume_workitem_ctx
{
        WDFDEVICE vhci;
        WCHAR link_buf[MAX_PATH];
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(volume_workitem_ctx, get_volume_workitem_ctx)

/*
 * ExAcquireRundownProtection must be called earlier.
 */
struct rundown_guard
{
        explicit rundown_guard(_Inout_ EX_RUNDOWN_REF &r) : ref(r) {}
        ~rundown_guard() { ExReleaseRundownProtection(&ref); }

        rundown_guard(const rundown_guard&) = delete;
        rundown_guard& operator=(const rundown_guard&) = delete;

        EX_RUNDOWN_REF &ref;
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto sizeof_VOLUME_DISK_EXTENTS(_In_ ULONG cnt)
{
        return sizeof(VOLUME_DISK_EXTENTS) + (cnt > 1 ? (cnt - 1)*sizeof(*VOLUME_DISK_EXTENTS::Extents) : 0);
}

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED auto get_requestor_session_id(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::get_requestor_session_id(irp);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_requestor_owner(_In_ WDFREQUEST request, _Out_ vhci::device_owner &owner)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        owner.session_id = libdrv::get_requestor_session_id(irp);
        return libdrv::get_requestor_sid(irp, owner.sid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_session_isolation(_In_ WDFREQUEST request, _Inout_ vhci::device_owner &owner)
{
        PAGED_CODE();

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                if (!owner.has_session()) {
                        Trace(TRACE_LEVEL_ERROR, "session_id is required for session isolation");
                        return STATUS_INVALID_PARAMETER;
                }
                return STATUS_SUCCESS;
        }

        owner = { .session_id = get_requestor_session_id(request) };
        if (!owner.has_session()) {
                Trace(TRACE_LEVEL_ERROR, "get_requestor_session_id");
                return STATUS_ACCESS_DENIED;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_user_isolation(_In_ WDFREQUEST request, _Inout_ vhci::device_owner &owner)
{
        PAGED_CODE();

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                if (!libdrv::is_valid(owner.sid)) {
                        Trace(TRACE_LEVEL_ERROR, "valid SID is required for user isolation");
                        return STATUS_INVALID_PARAMETER;
                }
                return STATUS_SUCCESS;
        }

        if (!owner.has_sid()) {
                auto st = get_requestor_owner(request, owner);
                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "get_requestor_owner %!STATUS!", st);
                        return STATUS_ACCESS_DENIED;
                }
                return STATUS_SUCCESS;
        }

        if (!libdrv::is_valid(owner.sid)) {
                Trace(TRACE_LEVEL_ERROR, "invalid SID");
                return STATUS_INVALID_PARAMETER;
        }

        auto irp = WdfRequestWdmGetIrp(request);
        if (!(libdrv::is_admin_request(irp) || is_caller_sid(irp, owner.sid))) {
                Trace(TRACE_LEVEL_ERROR, "caller is not admin and SID does not match");
                return STATUS_ACCESS_DENIED;
        }

        owner.session_id = libdrv::get_requestor_session_id(irp);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_harddisk_volume_path(_In_ const UNICODE_STRING &nt_device)
{
        PAGED_CODE();

        DECLARE_CONST_UNICODE_STRING(prefix, L"\\Device\\HarddiskVolume");
        if (!RtlPrefixUnicodeString(&prefix, &nt_device, true)) {
                return false;
        }

        const auto prefix_cch = length_cch(prefix);
        const auto total_cch = length_cch(nt_device);

        if (total_cch <= prefix_cch) {
                return false;
        }

        for (USHORT i = prefix_cch; i < total_cch; ++i) {
                if (!isdigit(nt_device.Buffer[i])) {
                        return false;
                }
        }

        return true;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_storage_device_number(_In_ DEVICE_OBJECT *devobj, _Out_ ULONG &disk_number)
{
        PAGED_CODE();
        disk_number = 0;

        KEVENT event;
        KeInitializeEvent(&event, NotificationEvent, false);

        STORAGE_DEVICE_NUMBER sdn;
        IO_STATUS_BLOCK iost{};

        auto irp = IoBuildDeviceIoControlRequest(
                IOCTL_STORAGE_GET_DEVICE_NUMBER, devobj,
                nullptr, 0, &sdn, sizeof(sdn),
                false, &event, &iost);

        if (!irp) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto st = IoCallDriver(devobj, irp);
        if (st == STATUS_PENDING) {
                KeWaitForSingleObject(&event, Executive, KernelMode, false, nullptr);
                st = iost.Status;
        }

        if (!NT_SUCCESS(st)) {
                return st;
        }

        if (iost.Information != sizeof(sdn)) {
                return STATUS_INFO_LENGTH_MISMATCH;
        }

        disk_number = sdn.DeviceNumber;
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_disk_pdo_on_device(
        _In_ DEVICE_OBJECT *disk_pdo,
        _In_ const UNICODE_STRING &expected_id,
        _In_ const GUID &container_id)
{
        PAGED_CODE();

        wchar_t parent_buf[max_device_id_len]{};
        ULONG req_sz{};
        DEVPROPTYPE prop_type{};

        auto st = IoGetDevicePropertyData(
                disk_pdo, &DEVPKEY_Device_Parent,
                LOCALE_NEUTRAL, 0, sizeof(parent_buf) - sizeof(*parent_buf), parent_buf,
                &req_sz, &prop_type);

        UNICODE_STRING actual_parent{};
        if (!(NT_SUCCESS(st) && prop_type == DEVPROP_TYPE_STRING &&
              NT_SUCCESS(RtlUnicodeStringInit(&actual_parent, parent_buf)))) {
                return false;
        }

        if (expected_id.Length && RtlEqualUnicodeString(&actual_parent, &expected_id, true)) {
                return true;
        }

        DECLARE_CONST_UNICODE_STRING(usb_prefix, L"USB\\");
        if (!(container_id != guid_null && RtlPrefixUnicodeString(&usb_prefix, &actual_parent, true))) {
                return false;
        }

        GUID disk_cid{};
        st = IoGetDevicePropertyData(
                disk_pdo, &DEVPKEY_Device_ContainerId,
                LOCALE_NEUTRAL, 0, sizeof(disk_cid), &disk_cid,
                &req_sz, &prop_type);

        return NT_SUCCESS(st) && prop_type == DEVPROP_TYPE_GUID && disk_cid == container_id;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_disk_on_device(_In_ ULONG disk_number, _In_ const device_attributes &attr)
{
        PAGED_CODE();

        if (!*attr.pdo_instance_id && attr.container_id == guid_null) {
                return false;
        }

        device_interfaces_handle list;
        auto st = get_device_interfaces(list, GUID_DEVINTERFACE_DISK);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoGetDeviceInterfaces(DISK) %!STATUS!", st);
                return false;
        }

        UNICODE_STRING expected_id{};
        if (*attr.pdo_instance_id) {
                st = RtlUnicodeStringInit(&expected_id, attr.pdo_instance_id);
                if (!NT_SUCCESS(st)) {
                        return false;
                }
        }

        for (auto p = list.get(); *p; ) {
                UNICODE_STRING link;
                st = RtlUnicodeStringInit(&link, p);
                if (!NT_SUCCESS(st)) {
                        break;
                }

                p += length_cch(link) + 1;

                FILE_OBJECT *file_obj{};
                DEVICE_OBJECT *dev_obj{};
                st = IoGetDeviceObjectPointer(&link, FILE_ANY_ACCESS, &file_obj, &dev_obj);
                if (!NT_SUCCESS(st)) {
                        continue;
                }
                wdm::object_reference file_ref(file_obj, false);

                ULONG num;
                st = get_storage_device_number(dev_obj, num);
                if (!(NT_SUCCESS(st) && num == disk_number)) {
                        continue;
                }

                wdm::object_reference disk_pdo(IoGetDeviceAttachmentBaseRef(dev_obj), false);
                if (disk_pdo && is_disk_pdo_on_device(disk_pdo.get<DEVICE_OBJECT>(), expected_id, attr.container_id)) {
                        return true;
                }
        }

        return false;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool verify_volume_on_device(_In_ HANDLE vol_handle, _In_ const device_attributes &attr)
{
        PAGED_CODE();

        VOLUME_DISK_EXTENTS single{};
        IO_STATUS_BLOCK iosb{};

        auto st = ZwDeviceIoControlFile(
                vol_handle, nullptr, nullptr, nullptr, &iosb,
                IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                nullptr, 0, &single, sizeof(single));

        if (st == STATUS_SUCCESS) {
                if (!(iosb.Information >= sizeof(single) && single.NumberOfDiskExtents)) {
                        return false;
                }
                return is_disk_on_device(single.Extents[0].DiskNumber, attr);
        }

        if (!(st == STATUS_BUFFER_OVERFLOW && iosb.Information >= sizeof(single.NumberOfDiskExtents) &&
              single.NumberOfDiskExtents > 1 && single.NumberOfDiskExtents <= 64)) {
                return false;
        }

        const auto cnt = single.NumberOfDiskExtents;
        const auto sz = sizeof_VOLUME_DISK_EXTENTS(cnt);

        unique_ptr buf(PagedPool, sz);
        if (!buf) {
                return false;
        }

        auto extents = buf.get<VOLUME_DISK_EXTENTS>();
        st = ZwDeviceIoControlFile(
                vol_handle, nullptr, nullptr, nullptr, &iosb,
                IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                nullptr, 0, extents, static_cast<ULONG>(sz));

        if (!(NT_SUCCESS(st) && iosb.Information >= sizeof(*extents) && extents->NumberOfDiskExtents)) {
                return false;
        }

        const auto actual_cnt = min(cnt, extents->NumberOfDiskExtents);
        if (iosb.Information < sizeof_VOLUME_DISK_EXTENTS(actual_cnt)) {
                return false;
        }

        for (ULONG i = 0; i < actual_cnt; ++i) {
                if (!is_disk_on_device(extents->Extents[i].DiskNumber, attr)) {
                        return false;
                }
        }

        return true;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS harden_volume_root(
        _In_ const UNICODE_STRING &nt_device,
        _In_ const sid_blob &user_sid,
        _In_ const sid_blob &logon_sid)
{
        PAGED_CODE();

        WCHAR root_buf[262]{};
        UNICODE_STRING root_path{ 0, sizeof(root_buf), root_buf };

        auto st = RtlUnicodeStringCopy(&root_path, &nt_device);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        st = RtlUnicodeStringCatString(&root_path, L"\\");
        if (!NT_SUCCESS(st)) {
                return st;
        }

        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &root_path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

        HANDLE h{};
        IO_STATUS_BLOCK iosb{};
        st = ZwOpenFile(
                &h, READ_CONTROL | WRITE_DAC | SYNCHRONIZE, &oa, &iosb,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_VERBOSE, "ZwOpenFile('%!USTR!') %!STATUS! (unformatted or unsupported root)",
                      &root_path, st);
                return STATUS_SUCCESS;
        }

        zw_handle close_root(h);
        st = libdrv::set_volume_dacl(h, user_sid, logon_sid);

        if (st == STATUS_INVALID_DEVICE_REQUEST || st == STATUS_NOT_SUPPORTED ||
            st == STATUS_NOT_IMPLEMENTED || st == STATUS_INVALID_PARAMETER) {
                Trace(TRACE_LEVEL_VERBOSE, "set_volume_dacl('%!USTR!') %!STATUS! (filesystem lacks persistent ACLs)",
                      &root_path, st);
                return STATUS_SUCCESS;
        }

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_WARNING, "set_volume_dacl('%!USTR!') %!STATUS!", &root_path, st);
        }

        return st;
}

_Function_class_(EVT_WDF_WORKITEM)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void NTAPI volume_arrival_worker(_In_ WDFWORKITEM wi)
{
        PAGED_CODE();

        auto &wctx = *get_volume_workitem_ctx(wi);
        auto &vctx = *get_vhci_ctx(wctx.vhci);

        rundown_guard release_rundown(vctx.volume_rundown);
        wdf::object_delete del(wi);

        if (get_flag(vctx.removing)) {
                return;
        }

        UNICODE_STRING vol_link;
        auto st = RtlUnicodeStringInit(&vol_link, wctx.link_buf);
        if (!NT_SUCCESS(st) || empty(vol_link)) {
                return;
        }

        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &vol_link, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

        HANDLE h{};
        IO_STATUS_BLOCK iosb{};
        st = ZwOpenFile(
                &h, READ_CONTROL | WRITE_DAC | SYNCHRONIZE, &oa, &iosb,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_VERBOSE, "ZwOpenFile('%!USTR!') %!STATUS!", &vol_link, st);
                return;
        }
        zw_handle close_vol(h);

        for (int port = 1; port <= vctx.devices_cnt; ++port) {
                if (get_flag(vctx.removing)) {
                        break;
                }

                auto device = vhci::get_device(wctx.vhci, port);
                if (!device) {
                        continue;
                }

                auto &dev = *get_device_ctx(device.get());
                if (!(dev.is_session_isolated() || dev.is_user_isolated())) {
                        continue;
                }

                auto &attr = dev.attributes();
                if (!libdrv::is_valid(attr.caller_user_sid)) {
                        continue;
                }

                if (!verify_volume_on_device(h, attr)) {
                        continue;
                }

                st = libdrv::set_volume_dacl(h, attr.caller_user_sid, attr.caller_logon_sid);
                Trace(NT_SUCCESS(st) ? TRACE_LEVEL_INFORMATION : TRACE_LEVEL_WARNING,
                      "PnP volume DACL hardening port %d ('%!USTR!') %!STATUS!",
                      port, &vol_link, st);
                break;
        }
}

_Function_class_(DRIVER_NOTIFICATION_CALLBACK_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS NTAPI volume_arrival_callback(_In_ PVOID notification_struct, _Inout_opt_ PVOID context)
{
        PAGED_CODE();

        auto note = static_cast<const DEVICE_INTERFACE_CHANGE_NOTIFICATION*>(notification_struct);
        auto vhci = static_cast<WDFDEVICE>(context);

        if (!(note && vhci && note->Event == GUID_DEVICE_INTERFACE_ARRIVAL &&
              note->SymbolicLinkName && !empty(*note->SymbolicLinkName))) {
                return STATUS_SUCCESS;
        }

        auto &vctx = *get_vhci_ctx(vhci);
        if (get_flag(vctx.removing) || !ExAcquireRundownProtection(&vctx.volume_rundown)) {
                return STATUS_SUCCESS;
        }

        WDF_WORKITEM_CONFIG cfg;
        WDF_WORKITEM_CONFIG_INIT(&cfg, volume_arrival_worker);
        cfg.AutomaticSerialization = false;

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, volume_workitem_ctx);
        attr.ParentObject = vhci;

        WDFWORKITEM wi{};
        auto st = WdfWorkItemCreate(&cfg, &attr, &wi);
        if (!NT_SUCCESS(st)) {
                ExReleaseRundownProtection(&vctx.volume_rundown);
                return STATUS_SUCCESS;
        }

        auto &wctx = *get_volume_workitem_ctx(wi);
        wctx.vhci = vhci;

        st = RtlStringCbCopyNW(
                wctx.link_buf, sizeof(wctx.link_buf),
                note->SymbolicLinkName->Buffer, note->SymbolicLinkName->Length);

        if (!NT_SUCCESS(st)) {
                WdfObjectDelete(wi);
                ExReleaseRundownProtection(&vctx.volume_rundown);
                return STATUS_SUCCESS;
        }

        WdfWorkItemEnqueue(wi);
        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_admin_request(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::is_admin_request(irp);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::init_requestor_isolation(_In_ WDFREQUEST request, _Inout_ vhci::isolation_data &iso)
{
        PAGED_CODE();
        using vhci::isolation;

        if (iso.mode != isolation::user && iso.owner.has_sid()) {
                Trace(TRACE_LEVEL_ERROR, "SID specified without user isolation");
                return STATUS_INVALID_PARAMETER;
        }

        switch (iso.mode) {
        case isolation::none:
                iso.owner = {};
                return STATUS_SUCCESS;
        case isolation::session:
                return init_session_isolation(request, iso.owner);
        case isolation::user:
                return init_user_isolation(request, iso.owner);
        }

        return STATUS_INVALID_PARAMETER;
}

/*
 * Resolves caller ownership and privilege.
 * For administrators (including KernelMode callers), only the session ID is recorded;
 * extracting a User SID is unnecessary because administrators bypass user isolation
 * restrictions, and would fail with STATUS_NO_TOKEN for KernelMode threads lacking tokens.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::get_caller_owner(
        _In_ WDFREQUEST request, _Out_ bool &is_admin, _Out_ vhci::device_owner &owner)
{
        PAGED_CODE();
        owner = {};

        is_admin = is_admin_request(request);
        if (is_admin) {
                owner.session_id = get_requestor_session_id(request);
                return STATUS_SUCCESS;
        }

        auto st = get_requestor_owner(request, owner);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "get_requestor_owner %!STATUS!", st);
        }
        return st;
}

/*
 * Evaluation order does not matter: session and user isolation modes are
 * mutually exclusive based on config.iso.mode, so checking session isolation
 * first cannot bypass user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_owner(_In_ const device_ctx &dev, _In_ const vhci::device_owner &owner)
{
        PAGED_CODE();

        if (dev.is_session_isolated()) {
                return dev.owner().session_id == owner.session_id;
        }

        if (dev.is_user_isolated()) {
                return dev.owner().sid == owner.sid;
        }

        return false;
}

/*
 * Evaluation order does not matter: session and user isolation modes are
 * mutually exclusive based on config.iso.mode, so checking session isolation
 * first cannot bypass user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const device_ctx &dev, _In_ const vhci::device_owner &owner)
{
        PAGED_CODE();

        if (dev.is_session_isolated()) {
                return dev.owner().session_id != owner.session_id;
        }

        if (dev.is_user_isolated()) {
                return dev.owner().sid != owner.sid;
        }

        return false;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const device_ctx &dev, _In_ const fileobject_ctx &fobj)
{
        PAGED_CODE();
        NT_ASSERT(!fobj.is_admin);
        return is_access_denied(dev, fobj.owner);
}

/*
 * Evaluation order matters: User SID takes precedence over Session ID.
 * User-isolated owners possess both a Session ID and a User SID; checking
 * has_session() first would match on session ID and bypass SID verification.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const vhci::device_owner &owner, _In_ const fileobject_ctx &fobj)
{
        PAGED_CODE();
        NT_ASSERT(!fobj.is_admin);

        if (owner.has_sid()) {
                return owner.sid != fobj.owner.sid;
        }

        if (owner.has_session()) {
                return owner.session_id != fobj.owner.session_id;
        }

        return false;
}

/*
 * Evaluation order does not affect logical correctness (both session ID and
 * User SID must match to grant access), but checking session ID first allows
 * cross-session requests to short-circuit quickly before the more expensive
 * token query and SID comparison.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const fileobject_ctx &fobj, _In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);

        if (fobj.owner.session_id != libdrv::get_requestor_session_id(irp)) {
                return true;
        }

        if (fobj.owner.has_sid()) {
                return !libdrv::is_caller_sid(irp, fobj.owner.sid);
        }

        return false;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::init_requestor_isolation(_In_ WDFREQUEST request, _Inout_ device_attributes &attr)
{
        PAGED_CODE();

        auto st = init_requestor_isolation(request, attr.config.iso);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        attr.caller_user_sid = {};
        attr.caller_logon_sid = {};

        auto &iso = attr.config.iso;
        if (iso.mode == vhci::isolation::none) {
                return STATUS_SUCCESS;
        }

        auto irp = WdfRequestWdmGetIrp(request);
        const bool user_mode = WdfRequestGetRequestorMode(request) != KernelMode;

        if (iso.mode == vhci::isolation::user) {
                attr.caller_user_sid = iso.owner.sid;
                if (user_mode && libdrv::is_caller_sid(irp, attr.caller_user_sid)) {
                        libdrv::get_requestor_logon_sid(irp, attr.caller_logon_sid);
                }
        } else if (iso.mode == vhci::isolation::session && user_mode) {
                libdrv::get_requestor_sid(irp, attr.caller_user_sid);
                libdrv::get_requestor_logon_sid(irp, attr.caller_logon_sid);
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::session::save_pdo_properties(_Inout_ device_attributes &attr, _In_ DEVICE_OBJECT *pdo)
{
        PAGED_CODE();

        if (!pdo) {
                return;
        }

        attr.pdo_instance_id[0] = L'\0';
        attr.container_id = {};

        ULONG req_sz{};
        DEVPROPTYPE type{};

        auto st = IoGetDevicePropertyData(
                pdo, &DEVPKEY_Device_InstanceId, LOCALE_NEUTRAL, 0,
                sizeof(attr.pdo_instance_id) - sizeof(WCHAR), attr.pdo_instance_id,
                &req_sz, &type);

        if (!NT_SUCCESS(st) || type != DEVPROP_TYPE_STRING) {
                attr.pdo_instance_id[0] = L'\0';
                Trace(TRACE_LEVEL_WARNING, "IoGetDevicePropertyData(InstanceId) %!STATUS!", st);
        }

        st = IoGetDevicePropertyData(
                pdo, &DEVPKEY_Device_ContainerId, LOCALE_NEUTRAL, 0,
                sizeof(attr.container_id), &attr.container_id,
                &req_sz, &type);

        if (!NT_SUCCESS(st) || type != DEVPROP_TYPE_GUID) {
                attr.container_id = {};
                Trace(TRACE_LEVEL_WARNING, "IoGetDevicePropertyData(ContainerId) %!STATUS!", st);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::isolate_device_volume(
        _In_ WDFREQUEST request,
        _In_ const device_ctx &dev,
        _In_ const UNICODE_STRING &nt_device,
        _In_ wchar_t drive_letter)
{
        PAGED_CODE();

        if (!is_harddisk_volume_path(nt_device)) {
                return STATUS_INVALID_PARAMETER;
        }

        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(
                &oa, const_cast<UNICODE_STRING*>(&nt_device),
                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

        HANDLE vol_handle{};
        IO_STATUS_BLOCK iosb{};
        auto st = ZwOpenFile(
                &vol_handle, READ_CONTROL | WRITE_DAC | SYNCHRONIZE, &oa, &iosb,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "ZwOpenFile('%!USTR!') %!STATUS!", &nt_device, st);
                return st;
        }
        zw_handle close_vol(vol_handle);

        const auto &attr = dev.attributes();
        if (!verify_volume_on_device(vol_handle, attr)) {
                Trace(TRACE_LEVEL_ERROR, "Volume '%!USTR!' does not belong to port %d", &nt_device, dev.port);
                return STATUS_ACCESS_DENIED;
        }

        if (drive_letter) {
                auto rm_st = libdrv::remove_global_drive_letter(drive_letter, nt_device);
                if (!NT_SUCCESS(rm_st) && rm_st != STATUS_OBJECT_NAME_NOT_FOUND) {
                        Trace(TRACE_LEVEL_WARNING, "remove_global_drive_letter('%wc:', '%!USTR!') %!STATUS!",
                              drive_letter, &nt_device, rm_st);
                }
        }

        sid_blob user_sid = attr.caller_user_sid;
        sid_blob logon_sid = attr.caller_logon_sid;

        auto irp = WdfRequestWdmGetIrp(request);
        if (!libdrv::is_valid(user_sid)) {
                if (dev.is_user_isolated()) {
                        user_sid = dev.owner().sid;
                } else {
                        libdrv::get_requestor_sid(irp, user_sid);
                }
        }

        if (!libdrv::is_valid(logon_sid) && WdfRequestGetRequestorMode(request) != KernelMode) {
                if (!libdrv::is_valid(user_sid) || libdrv::is_caller_sid(irp, user_sid)) {
                        libdrv::get_requestor_logon_sid(irp, logon_sid);
                }
        }

        if (!libdrv::is_valid(user_sid)) {
                return STATUS_NO_TOKEN;
        }

        st = libdrv::set_volume_dacl(vol_handle, user_sid, logon_sid);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "set_volume_dacl('%!USTR!') %!STATUS!", &nt_device, st);
                return st;
        }

        return harden_volume_root(nt_device, user_sid, logon_sid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::register_volume_notification(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        auto &ctx = *get_vhci_ctx(vhci);
        ExInitializeRundownProtection(&ctx.volume_rundown);

        auto drvobj = WdfDriverWdmGetDriverObject(WdfDeviceGetDriver(vhci));
        auto st = IoRegisterPlugPlayNotification(
                EventCategoryDeviceInterfaceChange,
                0,
                const_cast<GUID*>(&GUID_DEVINTERFACE_VOLUME),
                drvobj,
                volume_arrival_callback,
                vhci,
                &ctx.volume_notification);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoRegisterPlugPlayNotification(VOLUME) %!STATUS!", st);
        }

        return st;
}
