/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "isolation.h"
#include "trace.h"
#include "isolation.tmh"

#include "context.h"
#include "driver.h"
#include "vhci.h"
#include "device.h"
#include "persistent.h"

#include <libdrv/strconv.h>
#include <libdrv/security.h>
#include <libdrv/handle_ptr.h>
#include <libdrv/irp.h>
#include <libdrv/timeout.h>

#include <usbip/offsetof_ex.h>

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
        WCHAR link_buf[MAX_PATH];
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(volume_workitem_ctx, get_volume_workitem_ctx)

struct adopt_rundown_t {};
constexpr adopt_rundown_t adopt_rundown;

struct rundown_guard
{
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        explicit rundown_guard(_Inout_ EX_RUNDOWN_REF &r) :
                ref(ExAcquireRundownProtection(&r) ? &r : nullptr) {}

        constexpr rundown_guard(_Inout_ EX_RUNDOWN_REF &r, _In_ adopt_rundown_t) : ref(&r) {}

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~rundown_guard()
        {
                if (ref) {
                        ExReleaseRundownProtection(ref);
                }
        }

        rundown_guard(const rundown_guard&) = delete;
        rundown_guard& operator=(const rundown_guard&) = delete;

        constexpr explicit operator bool() const { return ref; }

        constexpr auto release()
        {
                auto ptr = ref;
                ref = nullptr;
                return ptr;
        }

        EX_RUNDOWN_REF *ref{};
};

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
PAGED auto parse_harddisk_volume_number(_In_ const UNICODE_STRING &nt_device, _Out_ ULONG &vol_num)
{
        PAGED_CODE();
        vol_num = 0;

        DECLARE_CONST_UNICODE_STRING(prefix, L"\\Device\\HarddiskVolume");
        if (!RtlPrefixUnicodeString(&prefix, &nt_device, true)) {
                return false;
        }

        const auto prefix_cch = length_cch(prefix);
        const auto total_cch = length_cch(nt_device);

        if (total_cch <= prefix_cch) {
                return false;
        }

        ULONG num = 0;

        for (USHORT i = prefix_cch; i < total_cch; ++i) {
                auto ch = nt_device.Buffer[i];
                if (!isdigit(ch)) {
                        vol_num = 0;
                        return false;
                }
                if (num > (MAXULONG - (ch - L'0')) / 10) {
                        vol_num = 0;
                        return false;
                }
                num = num * 10 + (ch - L'0');
        }

        vol_num = num;
        return true;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_harddisk_volume_path(_In_ const UNICODE_STRING &nt_device)
{
        PAGED_CODE();
        ULONG vol_num;
        return parse_harddisk_volume_number(nt_device, vol_num);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr LONG pack_volume_letter(_In_ ULONG vol_num, _In_ wchar_t letter)
{
        return static_cast<LONG>((vol_num << 16) | static_cast<USHORT>(letter));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void save_volume_letter(_Inout_ device_attributes &attr, _In_ ULONG vol_num, _In_ wchar_t letter)
{
        if (!(vol_num <= 0x7FFF && letter)) {
                return;
        }

        const auto packed = pack_volume_letter(vol_num, letter);

        for (auto &slot : attr.volume_letters) {
                auto cur = InterlockedCompareExchange(&slot, packed, 0);
                if (!cur) {
                        return;
                }
                if (static_cast<ULONG>(cur >> 16) == vol_num) {
                        InterlockedExchange(&slot, packed);
                        return;
                }
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
wchar_t lookup_volume_letter(_In_ const device_attributes &attr, _In_ ULONG vol_num)
{
        if (vol_num > 0x7FFF) {
                return L'\0';
        }

        for (const auto &slot : attr.volume_letters) {
                auto cur = InterlockedCompareExchange(const_cast<LONG*>(&slot), 0, 0);
                if (!cur) {
                        break;
                }
                if (static_cast<ULONG>(cur >> 16) == vol_num) {
                        return static_cast<wchar_t>(cur & 0xFFFF);
                }
        }

        return L'\0';
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto query_symbolic_link_target(
        _In_ const UNICODE_STRING &link_name, _Inout_ UNICODE_STRING &target)
{
        PAGED_CODE();

        OBJECT_ATTRIBUTES attr;
        InitializeObjectAttributes(&attr, const_cast<UNICODE_STRING*>(&link_name),
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

        HANDLE hlink;
        auto st = ZwOpenSymbolicLinkObject(&hlink, SYMBOLIC_LINK_QUERY, &attr);
        if (!NT_SUCCESS(st)) {
                return st;
        }
        zw_handle del(hlink);

        return ZwQuerySymbolicLinkObject(hlink, &target, nullptr);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto open_for_dacl(_Out_ zw_handle &handle, _In_ const UNICODE_STRING &path)
{
        PAGED_CODE();

        OBJECT_ATTRIBUTES attr;
        InitializeObjectAttributes(&attr, const_cast<UNICODE_STRING*>(&path),
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

        HANDLE h{};
        IO_STATUS_BLOCK iost;
        constexpr ULONG opts = FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE;

        auto st = ZwOpenFile(&h, READ_CONTROL | WRITE_DAC | SYNCHRONIZE, &attr, &iost,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, opts);

        handle.reset(h);
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_storage_device_number(_In_ DEVICE_OBJECT *devobj, _Out_ ULONG &disk_number)
{
        PAGED_CODE();

        STORAGE_DEVICE_NUMBER sdn;
        auto st = device_io_control(devobj, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                    nullptr, 0, &sdn, sizeof(sdn));

        disk_number = NT_SUCCESS(st) ? sdn.DeviceNumber : 0;
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_disk_pdo_on_device(
        _In_ DEVICE_OBJECT *disk_pdo, _In_ const UNICODE_STRING &expected_id, _In_ const GUID &container_id)
{
        PAGED_CODE();

        wchar_t parent_buf[max_device_id_len]{};
        ULONG req_sz;
        DEVPROPTYPE prop_type;

        auto st = IoGetDevicePropertyData(disk_pdo, &DEVPKEY_Device_Parent, LOCALE_NEUTRAL, 0,
                        sizeof(parent_buf) - sizeof(*parent_buf), parent_buf, &req_sz, &prop_type);

        UNICODE_STRING parent;
        if (!(NT_SUCCESS(st) && NT_SUCCESS(RtlUnicodeStringInit(&parent, parent_buf)))) {
                return false;
        }

        NT_ASSERT(prop_type == DEVPROP_TYPE_STRING);

        if (!empty(expected_id) && RtlEqualUnicodeString(&parent, &expected_id, true)) {
                return true;
        }

        if (container_id == guid_null) {
                return false;
        }

        DECLARE_CONST_UNICODE_STRING(usb_prefix, L"USB\\");
        if (!RtlPrefixUnicodeString(&usb_prefix, &parent, true)) {
                return false;
        }

        GUID disk_cid;
        st = IoGetDevicePropertyData(disk_pdo, &DEVPKEY_Device_ContainerId, LOCALE_NEUTRAL, 0,
                                     sizeof(disk_cid), &disk_cid, &req_sz, &prop_type);

        if (!NT_SUCCESS(st)) {
                return false;
        }

        NT_ASSERT(prop_type == DEVPROP_TYPE_GUID);
        NT_ASSERT(req_sz == sizeof(disk_cid));

        return disk_cid == container_id;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto is_disk_on_device(
        _In_ ULONG disk_number,
        _In_ const device_attributes &attr,
        _In_ const sid_blob &user_sid,
        _In_ const sid_blob &logon_sid)
{
        PAGED_CODE();

        if (!*attr.pdo_instance_id && attr.container_id == guid_null) {
                return false;
        }

        UNICODE_STRING expected_id{};

        if (*attr.pdo_instance_id) {
                auto st = RtlUnicodeStringInit(&expected_id, attr.pdo_instance_id);
                if (!NT_SUCCESS(st)) {
                        return false;
                }
        }

        device_interfaces_ptr list;

        auto st = get_device_interfaces(list, GUID_DEVINTERFACE_DISK);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoGetDeviceInterfaces(DISK) %!STATUS!", st);
                return false;
        }

        for (auto p = list.get(); *p; ) {

                UNICODE_STRING link;
                st = RtlUnicodeStringInit(&link, p);
                if (!NT_SUCCESS(st)) {
                        break;
                }

                p += length_cch(link) + 1;

                FILE_OBJECT *file_obj;
                DEVICE_OBJECT *dev_obj;

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

                if (is_disk_pdo_on_device(disk_pdo.get<DEVICE_OBJECT>(), expected_id, attr.container_id)) {
                        zw_handle disk_handle;
                        st = open_for_dacl(disk_handle, link);
                        if (NT_SUCCESS(st)) {
                                st = libdrv::set_volume_dacl(disk_handle.get(), user_sid, logon_sid);
                        }
                        if (!NT_SUCCESS(st)) {
                                Trace(TRACE_LEVEL_WARNING, "set_volume_dacl(disk '%!USTR!') %!STATUS!", &link, st);
                        }
                        return true;
                }
        }

        return false;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool verify_volume_on_device(
        _In_ HANDLE vol_handle,
        _In_ const device_attributes &attr,
        _In_ const sid_blob &user_sid,
        _In_ const sid_blob &logon_sid)
{
        PAGED_CODE();

        VOLUME_DISK_EXTENTS single;
        IO_STATUS_BLOCK iost;

        auto st = ZwDeviceIoControlFile(vol_handle, nullptr, nullptr, nullptr, &iost,
                        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &single, sizeof(single));

        switch (st) {
        case STATUS_SUCCESS:
                static_assert(NT_SUCCESS(STATUS_PENDING)); // why it is not used
                return iost.Information == sizeof(single) && single.NumberOfDiskExtents == 1 &&
                       is_disk_on_device(single.Extents[0].DiskNumber, attr, user_sid, logon_sid);
        case STATUS_BUFFER_OVERFLOW:
                static_assert(!offsetof(VOLUME_DISK_EXTENTS, NumberOfDiskExtents));
                if (!(iost.Information >= sizeof(single.NumberOfDiskExtents) && // only first member NumberOfDiskExtents is filled
                      single.NumberOfDiskExtents > 1 && single.NumberOfDiskExtents <= 64)) { // upper limit is arbitrary
                        return false;
                }
                break;
        default:
                return false;
        }

        const auto cnt = single.NumberOfDiskExtents;
        const auto sz = sizeof_anysize_struct(VOLUME_DISK_EXTENTS, Extents, cnt);

        unique_ptr buf(PagedPool, sz);
        if (!buf) {
                return false;
        }
        auto extents = buf.get<VOLUME_DISK_EXTENTS>();

        st = ZwDeviceIoControlFile(vol_handle, nullptr, nullptr, nullptr, &iost,
                IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, extents, static_cast<ULONG>(sz));

        if (!(st == STATUS_SUCCESS && iost.Information >= sizeof(*extents) &&
              extents->NumberOfDiskExtents && extents->NumberOfDiskExtents <= cnt &&
              iost.Information >= sizeof_anysize_struct(VOLUME_DISK_EXTENTS, Extents, extents->NumberOfDiskExtents))) {
                return false;
        }

        for (ULONG i = 0; i < extents->NumberOfDiskExtents; ++i) {
                if (!is_disk_on_device(extents->Extents[i].DiskNumber, attr, user_sid, logon_sid)) {
                        return false;
                }
        }

        return true;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdf::ObjectRef find_volume_device(
        _In_ WDFDEVICE vhci_dev, _Inout_ vhci_ctx &vhci, _In_ HANDLE vol_handle)
{
        PAGED_CODE();

        for (int port = 1; port <= vhci.devices_cnt; ++port) {

                if (get_flag(vhci.removing)) {
                        break;
                }

                auto device = vhci::get_device(vhci_dev, port);
                if (!device) {
                        continue;
                }

                auto &dev = *get_device_ctx(device.get());
                if (!(dev.is_session_isolated() || dev.is_user_isolated())) {
                        continue;
                }

                auto &attr = dev.attributes();
                if (libdrv::is_valid(attr.caller_user_sid) &&
                    verify_volume_on_device(vol_handle, attr, attr.caller_user_sid, attr.caller_logon_sid)) {
                        return device;
                }
        }

        return {};
}

/**
 * @see volume_arrival_cb
 */
_Function_class_(EVT_WDF_WORKITEM)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void NTAPI on_volume_arrival(_In_ WDFWORKITEM wi)
{
        PAGED_CODE();
        auto &ctx = *get_volume_workitem_ctx(wi);

        auto vhci_dev = static_cast<WDFDEVICE>(WdfWorkItemGetParentObject(wi));
        auto &vhci = *get_vhci_ctx(vhci_dev);

        rundown_guard guard(vhci.volume_rundown, adopt_rundown); // release after deleting wi
        wdf::object_delete del(wi);

        if (get_flag(vhci.removing)) {
                return;
        }

        UNICODE_STRING vol_link;
        auto st = RtlUnicodeStringInit(&vol_link, ctx.link_buf);
        if (!NT_SUCCESS(st) || empty(vol_link)) {
                return;
        }

        zw_handle h;
        st = open_for_dacl(h, vol_link);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_VERBOSE, "ZwOpenFile('%!USTR!') %!STATUS!", &vol_link, st);
                return;
        }

        auto device = find_volume_device(vhci_dev, vhci, h.get());
        if (!device) {
                return;
        }

        auto &dev = *get_device_ctx(device.get());
        auto &attr = dev.attributes();

        st = libdrv::set_volume_dacl(h.get(), attr.caller_user_sid, attr.caller_logon_sid);

        Trace(NT_SUCCESS(st) ? TRACE_LEVEL_INFORMATION : TRACE_LEVEL_WARNING,
              "PnP volume DACL hardening port %d ('%!USTR!') %!STATUS!", dev.port, &vol_link, st);

        h.reset();

        wchar_t nt_buf[MAX_PATH];
        UNICODE_STRING nt_device {
                .MaximumLength = sizeof(nt_buf),
                .Buffer = nt_buf
        };

        auto link_st = query_symbolic_link_target(vol_link, nt_device);
        if (!NT_SUCCESS(link_st)) {
                if (!is_harddisk_volume_path(vol_link)) {
                        return;
                }
                nt_device = vol_link;
        }

        ULONG vol_num;
        if (!parse_harddisk_volume_number(nt_device, vol_num)) {
                return;
        }

        auto interval = wdm::make_timeout(50*wdm::msec, wdm::period::relative);

        for (int attempt = 0; attempt < 20; ++attempt) {
                if (get_flag(vhci.removing) || get_flag(dev.unplugged)) {
                        break;
                }

                if (lookup_volume_letter(attr, vol_num)) {
                        break;
                }

                wchar_t letter{};
                auto rem_st = libdrv::remove_global_drive_letter(nt_device, letter);
                if (NT_SUCCESS(rem_st)) {
                        save_volume_letter(attr, vol_num, letter);
                        Trace(TRACE_LEVEL_INFORMATION,
                              "PnP volume removed global drive '%wc:' for port %d ('%!USTR!')",
                              letter, dev.port, &nt_device);
                        break;
                }

                KeDelayExecutionThread(KernelMode, false, &interval);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdf::object_delete create_workitem(_In_ WDFOBJECT parent)
{
        PAGED_CODE();

        WDF_WORKITEM_CONFIG cfg;
        WDF_WORKITEM_CONFIG_INIT(&cfg, on_volume_arrival);
        cfg.AutomaticSerialization = false;

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, volume_workitem_ctx);
        attr.ParentObject = parent;

        WDFWORKITEM wi;

        auto st = WdfWorkItemCreate(&cfg, &attr, &wi);
        if (!NT_SUCCESS(st)) {
                return {};
        }

        return wdf::object_delete(wi);
}

/**
 * Device-interface change notifications are informational broadcasts (no veto),
 * and hardening here is best-effort, so always return STATUS_SUCCESS.
 *
 * Offload volume open/I/O/PnP queries to a work item instead of running them
 * inline: the callback runs on a PnP thread holding PnP locks, where
 * synchronous I/O or device queries can deadlock or stall PnP dispatch.
 *
 * @see on_volume_arrival
 */
_Function_class_(DRIVER_NOTIFICATION_CALLBACK_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS NTAPI volume_arrival_cb(_In_ void *notif_struct, _Inout_opt_ void *context)
{
        PAGED_CODE();
        auto note = static_cast<const DEVICE_INTERFACE_CHANGE_NOTIFICATION*>(notif_struct);

        auto vhci_dev = static_cast<WDFDEVICE>(context);
        NT_ASSERT(vhci_dev);

        if (!(note->Event == GUID_DEVICE_INTERFACE_ARRIVAL &&
              note->SymbolicLinkName && !empty(*note->SymbolicLinkName))) {
                return STATUS_SUCCESS;
        }

        auto &vhci = *get_vhci_ctx(vhci_dev);
        if (get_flag(vhci.removing)) {
                return STATUS_SUCCESS;
        }

        rundown_guard guard(vhci.volume_rundown);
        if (!guard) {
                return STATUS_SUCCESS;
        }

        auto wi = create_workitem(vhci_dev);
        if (!wi) {
                return STATUS_SUCCESS;
        }
        auto &ctx = *get_volume_workitem_ctx(wi.get());

        auto st = RtlStringCbCopyNW(ctx.link_buf, sizeof(ctx.link_buf),
                        note->SymbolicLinkName->Buffer, note->SymbolicLinkName->Length);

        if (NT_SUCCESS(st)) { // ownership transferred to on_volume_arrival
                WdfWorkItemEnqueue(wi.release<WDFWORKITEM>());
                guard.release();
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto resolve_volume_sids(
        _In_ WDFREQUEST request, _In_ const device_ctx &dev,
        _Out_ sid_blob &user_sid, _Out_ sid_blob &logon_sid)
{
        PAGED_CODE();

        auto irp = WdfRequestWdmGetIrp(request);
        const auto &attr = dev.attributes();

        user_sid = attr.caller_user_sid;
        logon_sid = attr.caller_logon_sid; // optional

        if (!libdrv::is_valid(user_sid)) {
                if (dev.is_user_isolated()) {
                        user_sid = dev.owner().sid;
                        NT_ASSERT(libdrv::is_valid(user_sid));
                } else {
                        auto st = libdrv::get_requestor_sid(irp, user_sid);
                        if (!NT_SUCCESS(st)) {
                                return STATUS_NO_TOKEN;
                        }
                }
        }

        if (!libdrv::is_valid(logon_sid) &&
            WdfRequestGetRequestorMode(request) != KernelMode &&
            libdrv::is_caller_sid(irp, user_sid)) {
                libdrv::get_requestor_logon_sid(irp, logon_sid); // on error it stays empty and set_volume_dacl skips it
        }

        return STATUS_SUCCESS;
}

_Function_class_(IO_SESSION_NOTIFICATION_FUNCTION)
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS NTAPI session_notification_callback(
        _In_ void *session_obj,
        _In_ void* /*io_obj*/,
        _In_ ULONG event,
        _In_ void *context,
        _In_reads_bytes_opt_(payload_len) void* /*payload*/,
        [[maybe_unused]] _In_ ULONG payload_len)
{
        PAGED_CODE();

        if (!(event == IoSessionEventLogoff || event == IoSessionEventTerminated)) {
                return STATUS_SUCCESS;
        }

        auto vhci_dev = static_cast<WDFDEVICE>(context);
        NT_ASSERT(vhci_dev);

        auto &vhci = *get_vhci_ctx(vhci_dev);
        if (get_flag(vhci.removing)) {
                return STATUS_SUCCESS;
        }

        IO_SESSION_STATE_INFORMATION info;
        auto st = IoGetContainerInformation(IoSessionStateInformation, session_obj, &info, sizeof(info));
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoGetContainerInformation %!STATUS!", st);
                return STATUS_SUCCESS;
        }

        vhci::device_owner owner{ .session_id = info.SessionId };
        if (!owner.has_session()) {
                return STATUS_SUCCESS;
        }

        auto stopped = stop_attach_attempts(vhci, 0, &owner);
        Trace(TRACE_LEVEL_INFORMATION, "Session %lu, event %lu, stopped %d attach attempt(s)",
              owner.session_id, event, stopped);

        async_detach_and_delete_all(vhci_dev, &owner);
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
PAGED NTSTATUS usbip::session::init_requestor_isolation(
        _In_ WDFREQUEST request, _Inout_ vhci::isolation_data &iso)
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
                if (dev.owner().session_id != owner.session_id) {
                        return false;
                }
                auto &user_sid = dev.attributes().caller_user_sid;
                return !(owner.has_sid() && libdrv::is_valid(user_sid)) || user_sid == owner.sid;
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
                if (dev.owner().session_id != owner.session_id) {
                        return true;
                }
                auto &user_sid = dev.attributes().caller_user_sid;
                return libdrv::is_valid(user_sid) && user_sid != owner.sid;
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
PAGED bool usbip::session::is_access_denied(_In_ const device_attributes &attr, _In_ const fileobject_ctx &fobj)
{
        PAGED_CODE();
        NT_ASSERT(!fobj.is_admin);

        auto &owner = attr.config.iso.owner;

        if (owner.has_sid()) {
                return owner.sid != fobj.owner.sid;
        }

        if (owner.has_session()) {
                if (owner.session_id != fobj.owner.session_id) {
                        return true;
                }
                return libdrv::is_valid(attr.caller_user_sid) && attr.caller_user_sid != fobj.owner.sid;
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

/*
 * attr.config.iso is mandatory for USB device isolation; caller_user_sid is also
 * captured for user-mode isolation::none attaches (per-user port quota accounting)
 * and caller_logon_sid is stored for volume DACL hardening (logon SIDs may be absent
 * and empty SIDs are handled by volume isolation).
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::init_requestor_isolation(
        _In_ WDFREQUEST request, _Inout_ device_attributes &attr)
{
        PAGED_CODE();

        auto st = init_requestor_isolation(request, attr.config.iso);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        auto &iso = attr.config.iso;

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                if (iso.mode == vhci::isolation::user && !libdrv::is_valid(attr.caller_user_sid)) {
                        attr.caller_user_sid = iso.owner.sid;
                }
                return STATUS_SUCCESS;
        }

        attr.caller_user_sid = {};
        attr.caller_logon_sid = {};

        auto irp = WdfRequestWdmGetIrp(request);

        if (iso.mode == vhci::isolation::user) {
                attr.caller_user_sid = iso.owner.sid;
                if (libdrv::is_caller_sid(irp, attr.caller_user_sid)) {
                        libdrv::get_requestor_logon_sid(irp, attr.caller_logon_sid);
                }
        } else if (iso.mode == vhci::isolation::session) {
                st = libdrv::get_requestor_sid(irp, attr.caller_user_sid);
                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "get_requestor_sid %!STATUS!", st);
                        return STATUS_ACCESS_DENIED;
                }
                libdrv::get_requestor_logon_sid(irp, attr.caller_logon_sid);
        } else {
                libdrv::get_requestor_sid(irp, attr.caller_user_sid);
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::session::save_pdo_properties(
        _Inout_ device_attributes &attr, _In_opt_ DEVICE_OBJECT *pdo)
{
        PAGED_CODE();

        NT_ASSERT(!*attr.pdo_instance_id);
        NT_ASSERT(attr.container_id == guid_null);

        if (!pdo) {
                return;
        }

        ULONG req_sz;
        DEVPROPTYPE type;

        auto st = IoGetDevicePropertyData(pdo, &DEVPKEY_Device_InstanceId, LOCALE_NEUTRAL, 0,
                                sizeof(attr.pdo_instance_id) - sizeof(*attr.pdo_instance_id),
                                attr.pdo_instance_id, &req_sz, &type);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_WARNING, "IoGetDevicePropertyData(InstanceId) %!STATUS!", st);
        } else {
                NT_ASSERT(type == DEVPROP_TYPE_STRING);
        }

        st = IoGetDevicePropertyData(pdo, &DEVPKEY_Device_ContainerId, LOCALE_NEUTRAL, 0,
                        sizeof(attr.container_id), &attr.container_id, &req_sz, &type);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_WARNING, "IoGetDevicePropertyData(ContainerId) %!STATUS!", st);
        } else {
                NT_ASSERT(type == DEVPROP_TYPE_GUID);
                NT_ASSERT(req_sz == sizeof(attr.container_id));
        }
}

/*
 * Complements PnP volume arrival hardening (on_volume_arrival):
 * 1. Deletes \GLOBAL??\<drive_letter>: in kernel mode after MountMgr assigns it
 *    (standard users get ERROR_ACCESS_DENIED in userspace) so libusbip can
 *    re-create the drive letter in the caller's session DosDevices, or returns
 *    the drive letter already removed by on_volume_arrival.
 * 2. Hardens the raw volume (\Device\HarddiskVolumeN) and its underlying disk
 *    device stack (GUID_DEVINTERFACE_DISK / \Device\HarddiskN\DR0) DACLs without
 *    modifying persistent on-disk filesystem ACLs.
 * 3. Falls back to the IOCTL caller's token when caller_user_sid/caller_logon_sid
 *    were not captured at attach time (e.g. persistent devices).
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::isolate_device_volume(
        _In_ WDFREQUEST request,
        _Inout_ device_ctx &dev,
        _In_ const UNICODE_STRING &nt_device,
        _Inout_ wchar_t &drive_letter)
{
        PAGED_CODE();

        ULONG vol_num;
        if (!parse_harddisk_volume_number(nt_device, vol_num)) {
                return STATUS_INVALID_PARAMETER;
        }

        zw_handle vol_handle;
        auto st = open_for_dacl(vol_handle, nt_device);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "ZwOpenFile('%!USTR!') %!STATUS!", &nt_device, st);
                return st;
        }

        sid_blob user_sid;
        sid_blob logon_sid;

        st = resolve_volume_sids(request, dev, user_sid, logon_sid);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        if (!verify_volume_on_device(vol_handle.get(), dev.attributes(), user_sid, logon_sid)) {
                Trace(TRACE_LEVEL_ERROR, "Volume '%!USTR!' does not belong to port %d", &nt_device, dev.port);
                return STATUS_ACCESS_DENIED;
        }

        if (drive_letter) {
                auto ret = libdrv::remove_global_drive_letter(drive_letter, nt_device);
                if (NT_SUCCESS(ret)) {
                        save_volume_letter(dev.attributes(), vol_num, drive_letter);
                } else if (ret != STATUS_OBJECT_NAME_NOT_FOUND) {
                        Trace(TRACE_LEVEL_WARNING, "remove_global_drive_letter('%wc:', '%!USTR!') %!STATUS!",
                              drive_letter, &nt_device, ret);
                }
        } else {
                drive_letter = lookup_volume_letter(dev.attributes(), vol_num);
                if (!drive_letter) {
                        wchar_t removed{};
                        auto ret = libdrv::remove_global_drive_letter(nt_device, removed);
                        if (NT_SUCCESS(ret)) {
                                drive_letter = removed;
                                save_volume_letter(dev.attributes(), vol_num, removed);
                        }
                }
        }

        st = libdrv::set_volume_dacl(vol_handle.get(), user_sid, logon_sid);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "set_volume_dacl('%!USTR!') %!STATUS!", &nt_device, st);
        }
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::register_volume_notification(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();
        auto &ctx = *get_vhci_ctx(vhci);

        auto drv = WdfDeviceGetDriver(vhci);
        auto drvobj = WdfDriverWdmGetDriverObject(drv);

        auto st = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange, 0,
                                const_cast<GUID*>(&GUID_DEVINTERFACE_VOLUME), drvobj,
                                volume_arrival_cb, vhci, &ctx.volume_notification);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoRegisterPlugPlayNotification(VOLUME) %!STATUS!", st);
        }

        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::session::unregister_volume_notification(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        if (auto &ctx = *get_vhci_ctx(vhci);
            auto h = InterlockedExchangePointer(&ctx.volume_notification, nullptr)) {

                NT_VERIFY(NT_SUCCESS(IoUnregisterPlugPlayNotificationEx(h)));
                ExWaitForRundownProtectionRelease(&ctx.volume_rundown);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::register_session_notification(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();
        auto &ctx = *get_vhci_ctx(vhci);

        IO_SESSION_STATE_NOTIFICATION note {
                .Size = sizeof(note),
                .Flags = 0,
                .IoObject = WdfDeviceWdmGetDeviceObject(vhci),
                .EventMask = IO_SESSION_STATE_LOGOFF_EVENT | IO_SESSION_STATE_TERMINATION_EVENT,
                .Context = vhci,
        };

        auto st = IoRegisterContainerNotification(
                        IoSessionStateNotification,
                        reinterpret_cast<PIO_CONTAINER_NOTIFICATION_FUNCTION>(session_notification_callback),
                        &note, sizeof(note), &ctx.session_notification);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoRegisterContainerNotification %!STATUS!", st);
        }

        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::session::unregister_session_notification(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        auto &ctx = *get_vhci_ctx(vhci);
        if (auto h = InterlockedExchangePointer(&ctx.session_notification, nullptr)) {
                IoUnregisterContainerNotification(h);
        }
}
