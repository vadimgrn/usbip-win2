/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "volume_isolation.h"
#include "trace.h"
#include "volume_isolation.tmh"

#include "driver.h"
#include "vhci.h"

#include <libdrv/strconv.h>
#include <libdrv/security.h>
#include <libdrv/handle_ptr.h>
#include <libdrv/irp.h>
#include <libdrv/rundown.h>
#include <libdrv/timeout.h>

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

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto is_valid_volume_number(_In_ LONG64 vol_num)
{
        return vol_num > 0 && vol_num <= MAXLONG;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto parse_harddisk_volume_number(_In_ const UNICODE_STRING &nt_device, _Out_ LONG &vol_num)
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

        LONG64 num{};

        for (USHORT i = prefix_cch; i < total_cch; ++i) {

                auto ch = nt_device.Buffer[i];
                if (!isdigit(ch)) {
                        return false;
                }

                if (num = 10*num + (ch - L'0'); num > MAXLONG) {
                        break;
                }
        }

        if (!is_valid_volume_number(num)) {
                return false;
        }

        vol_num = static_cast<LONG>(num);
        return true;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void save_volume_letter(_Inout_ device_attributes &attr, _In_ LONG vol_num, _In_ wchar_t letter)
{
        if (is_drive_letter(letter) && is_valid_volume_number(vol_num)) {
                auto idx = ascii_toupper(letter) - L'A';
                InterlockedExchange(&attr.volume_letters[idx], vol_num);
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto lookup_volume_letter(_In_ const device_attributes &attr, _In_ LONG vol_num)
{
        if (!is_valid_volume_number(vol_num)) {
                return L'\0';
        }

        for (int i = 0; i < ARRAYSIZE(attr.volume_letters); ++i) {
                auto cur = InterlockedCompareExchange(const_cast<LONG*>(&attr.volume_letters[i]), 0, 0);
                if (cur == vol_num) {
                        return static_cast<wchar_t>(L'A' + i);
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

        HANDLE h;
        auto st = ZwOpenSymbolicLinkObject(&h, SYMBOLIC_LINK_QUERY, &attr);
        if (!NT_SUCCESS(st)) {
                return st;
        }
        zw_handle del(h);

        return ZwQuerySymbolicLinkObject(h, &target, nullptr);
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

        auto st = ZwOpenFile(&h, READ_CONTROL | WRITE_DAC | SYNCHRONIZE, &attr, &iost,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);

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

        wchar_t parent_buf[max_device_id_len]{}; // must be zeroed
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
PAGED auto is_disk_link_on_device(
        _In_ const UNICODE_STRING &link,
        _In_ ULONG disk_number,
        _In_ const UNICODE_STRING &expected_id,
        _In_ const GUID &container_id)
{
        PAGED_CODE();

        FILE_OBJECT *file_obj;
        DEVICE_OBJECT *dev_obj;

        auto st = IoGetDeviceObjectPointer(const_cast<UNICODE_STRING*>(&link), FILE_ANY_ACCESS, &file_obj, &dev_obj);
        if (!NT_SUCCESS(st)) {
                return false;
        }
        wdm::object_reference del(file_obj, false);

        ULONG num;
        st = get_storage_device_number(dev_obj, num);
        if (!(NT_SUCCESS(st) && num == disk_number)) {
                return false;
        }

        wdm::object_reference disk_pdo(IoGetDeviceAttachmentBaseRef(dev_obj), false);
        return is_disk_pdo_on_device(disk_pdo.get<DEVICE_OBJECT>(), expected_id, container_id);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void harden_disk_dacl(
        _In_ const UNICODE_STRING &link, _In_ const sid_blob &user_sid, _In_ const sid_blob &logon_sid)
{
        PAGED_CODE();

        zw_handle h;
        auto st = open_for_dacl(h, link);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_WARNING, "open_for_dacl(disk '%!USTR!') %!STATUS!", &link, st);
                return;
        }

        st = libdrv::set_volume_dacl(h.get(), user_sid, logon_sid);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_WARNING, "set_volume_dacl(disk '%!USTR!') %!STATUS!", &link, st);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto verify_and_harden_disk(
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

                if (is_disk_link_on_device(link, disk_number, expected_id, attr.container_id)) {
                        harden_disk_dacl(link, user_sid, logon_sid);
                        return true;
                }
        }

        return false;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool verify_multi_extent_volume(
        _In_ HANDLE vol_handle,
        _In_ ULONG cnt,
        _In_ const device_attributes &attr,
        _In_ const sid_blob &user_sid,
        _In_ const sid_blob &logon_sid)
{
        PAGED_CODE();

        const auto sz = sizeof_anysize_struct(VOLUME_DISK_EXTENTS, Extents, cnt);

        unique_ptr buf(PagedPool, sz);
        if (!buf) {
                return false;
        }
        auto extents = buf.get<VOLUME_DISK_EXTENTS>();

        IO_STATUS_BLOCK iost;
        auto st = ZwDeviceIoControlFile(vol_handle, nullptr, nullptr, nullptr, &iost,
                        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, extents, static_cast<ULONG>(sz));

        if (!(st == STATUS_SUCCESS && iost.Information >= sizeof(*extents) &&
              extents->NumberOfDiskExtents && extents->NumberOfDiskExtents <= cnt &&
              iost.Information >= sizeof_anysize_struct(VOLUME_DISK_EXTENTS, Extents, extents->NumberOfDiskExtents))) {
                return false;
        }

        for (ULONG i = 0; i < extents->NumberOfDiskExtents; ++i) {
                if (!verify_and_harden_disk(extents->Extents[i].DiskNumber, attr, user_sid, logon_sid)) {
                        return false;
                }
        }

        return true;
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
                       verify_and_harden_disk(single.Extents[0].DiskNumber, attr, user_sid, logon_sid);
        case STATUS_BUFFER_OVERFLOW:
                static_assert(!offsetof(VOLUME_DISK_EXTENTS, NumberOfDiskExtents));
                return iost.Information >= sizeof(single.NumberOfDiskExtents) && // only first member NumberOfDiskExtents is filled
                       single.NumberOfDiskExtents > 1 && single.NumberOfDiskExtents <= 64 && // upper limit is arbitrary
                       verify_multi_extent_volume(vol_handle, single.NumberOfDiskExtents, attr, user_sid, logon_sid);
        default:
                return false;
        }
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

                if (auto &attr = dev.attributes();
                    libdrv::is_valid(attr.caller_user_sid) &&
                    verify_volume_on_device(vol_handle, attr, attr.caller_user_sid, attr.caller_logon_sid)) {
                        return device;
                }
        }

        return {};
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void remove_volume_global_drive_letter(
        _Inout_ vhci_ctx &vhci, _Inout_ device_ctx &dev, _In_ const UNICODE_STRING &vol_link)
{
        PAGED_CODE();

        wchar_t nt_buf[MAX_PATH];
        UNICODE_STRING nt_device {
                .MaximumLength = sizeof(nt_buf),
                .Buffer = nt_buf
        };

        auto st = query_symbolic_link_target(vol_link, nt_device);
        if (!NT_SUCCESS(st)) {
                nt_device = vol_link;
        }

        LONG vol_num;
        if (!parse_harddisk_volume_number(nt_device, vol_num)) {
                return;
        }

        auto &attr = dev.attributes();
        auto interval = wdm::make_timeout(50*wdm::msec, wdm::period::relative);

        for (int attempt = 0; attempt < 20; ++attempt) {

                if (get_flag(vhci.removing) || get_flag(dev.unplugged)) {
                        break;
                }

                if (lookup_volume_letter(attr, vol_num)) {
                        break;
                }

                wchar_t letter;
                st = libdrv::remove_global_drive_letter(nt_device, letter);
                if (NT_SUCCESS(st)) {
                        save_volume_letter(attr, vol_num, letter);
                        Trace(TRACE_LEVEL_INFORMATION, "PnP volume removed global drive '%wc:' for port %d ('%!USTR!')",
                                                        letter, dev.port, &nt_device);
                        break;
                }

                KeDelayExecutionThread(KernelMode, false, &interval);
        }
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
        remove_volume_global_drive_letter(vhci, dev, vol_link);
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

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void remove_or_lookup_drive_letter(
        _Inout_ device_attributes &attr,
        _In_ LONG vol_num,
        _In_ const UNICODE_STRING &nt_device,
        _Inout_ wchar_t &drive_letter)
{
        PAGED_CODE();

        if (is_drive_letter(drive_letter)) {
                auto st = libdrv::remove_global_drive_letter(drive_letter, nt_device);
                if (NT_SUCCESS(st)) {
                        save_volume_letter(attr, vol_num, drive_letter);
                } else if (st != STATUS_OBJECT_NAME_NOT_FOUND) {
                        Trace(TRACE_LEVEL_WARNING, "remove_global_drive_letter('%wc:', '%!USTR!') %!STATUS!",
                                                    drive_letter, &nt_device, st);
                }
                return;
        }

        drive_letter = lookup_volume_letter(attr, vol_num);
        if (drive_letter) {
                return;
        }

        wchar_t removed;
        auto st = libdrv::remove_global_drive_letter(nt_device, removed);
        if (NT_SUCCESS(st)) {
                drive_letter = removed;
                save_volume_letter(attr, vol_num, removed);
        }
}

} // namespace


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

        LONG vol_num;
        if (!parse_harddisk_volume_number(nt_device, vol_num)) {
                return STATUS_INVALID_PARAMETER;
        }

        zw_handle h;
        auto st = open_for_dacl(h, nt_device);
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

        if (!verify_volume_on_device(h.get(), dev.attributes(), user_sid, logon_sid)) {
                Trace(TRACE_LEVEL_ERROR, "Volume '%!USTR!' does not belong to port %d", &nt_device, dev.port);
                return STATUS_ACCESS_DENIED;
        }

        remove_or_lookup_drive_letter(dev.attributes(), vol_num, nt_device, drive_letter);

        st = libdrv::set_volume_dacl(h.get(), user_sid, logon_sid);
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
