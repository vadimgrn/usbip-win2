/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "volume_isolation.h"
#include "../vhci.h"

#include "output.h"
#include "setupapi.h"
#include "last_error.h"
#include "stop_event.h"

#include <usbip/vhci.h>
#include <resources/messages.h>

#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <strsafe.h>

#include <thread>
#include <algorithm>

namespace
{

using namespace usbip;
static_assert(max_device_id_len == MAX_DEVICE_ID_LEN);

/*
 * Win32 volume GUID paths can arrive in several valid formats:
 * - `\\?\Volume{guid}\` from FindFirstVolume/GetVolumeNameForVolumeMountPoint
 * - `\\?\Volume{guid}` device interface paths without trailing slash
 * - `\\.\Volume{guid}`
 * - `\??\Volume{guid}`
 * - `Volume{guid}\`
 * - `Volume{guid}`
 *
 * The purpose of the function is to convert all of these variations into the single
 * canonical representation expected QueryDosDevice (which rejects `\\?\`, `\\.\`, `\??\` and trailing `\`).
 *
 * @param path volume GUID path
 */
auto to_dos_device_name(_In_ std::wstring_view path) noexcept
{
        for (auto prefix : { LR"(\\?\)", LR"(\\.\)", LR"(\??\)" }) {
                if (path.starts_with(prefix)) {
                        path.remove_prefix(std::char_traits<wchar_t>::length(prefix));
                        break;
                }
        }

        while (path.ends_with(L'\\')) {
                path.remove_suffix(1);
        }

        return path;
}

/*
 * Resolves a volume GUID path to its target NT device path (e.g. \Device\HarddiskVolumeN).
 * Returns std::nullopt on failure (logged to debug output); callers skip unresolvable
 * volumes as non-viable candidates during discovery without requiring error codes.
 *
 * Volume{guid} resolves to a single NT target (\Device\HarddiskVolumeN, ~25 chars),
 * so MAX_PATH easily fits the multi-string result.
 *
 * @param vol_guid_path volume GUID path
 * @see to_dos_device_name
 */
std::optional<std::wstring> resolve_volume_device(_In_ std::wstring_view vol_guid_path)
{
        std::wstring name(to_dos_device_name(vol_guid_path));

        if (wchar_t target[MAX_PATH];
            QueryDosDevice(name.c_str(), target, std::size(target))) {
                return target;
        }

        auto err = GetLastError();
        libusbip::output(L"QueryDosDevice('{}') error {}", name, err);
        return std::nullopt;
}

/*
 * Finds the drive letter mapped to the specified NT device path (e.g. \Device\HarddiskVolumeN).
 * Unassigned or non-existent drive letters fail QueryDosDevice and are skipped.
 *
 * QueryDosDevice returns a multi-string of all active/shadowed targets;
 * even if both session DosDevices and \GLOBAL?? map \Device\HarddiskVolumeN,
 * their combined length (~52 chars) fits well within MAX_PATH.
 *
 * NOTE on string comparison:
 * 1. Why <filesystem> (std::filesystem::path) is NOT used:
 *    - std::filesystem::path is designed for Win32 filesystem paths, whereas NT device targets
 *      (like \Device\HarddiskVolumeN) are Object Manager namespace symlink targets, not file paths.
 *    - Constructing std::filesystem::path(target) on every iteration of the drive letter loop
 *      ('A' through 'Z') incurs unnecessary heap allocations and redundant path decomposition/parsing.
 * 2. Why equal_ordinal is OK (and preferred):
 *    - Microsoft explicitly recommends CompareStringOrdinal (wrapped by usbip::equal_ordinal) for
 *      system object names, NT device paths, and internal identifiers because it performs exact,
 *      non-linguistic, binary-level case-insensitive comparisons without locale overhead or risk of
 *      linguistic distortion.
 *    - Both `target` (from QueryDosDevice) and `nt_device` are canonical NT device paths
 *      (e.g. \Device\HarddiskVolumeN) with matching forward/backward slash normalization, so ordinal
 *      case-insensitive matching is exact, correct, and zero-allocation.
 *
 * @param nt_device Target NT device path to match against QueryDosDevice results
 * @return Drive letter ('A'-'Z') if found, or L'\0' if no letter is assigned
 */
auto find_volume_drive_letter(_In_ std::wstring_view nt_device)
{
        if (nt_device.empty()) {
                return L'\0';
        }

        for (auto ch = L'A'; ch <= L'Z'; ++ch) {

                const wchar_t drive[] { ch, L':', L'\0' };

                if (wchar_t target[MAX_PATH];
                    QueryDosDevice(drive, target, std::size(target)) &&
                    equal_ordinal(target, nt_device, true)) {
                        return ch;
                }
        }

        return L'\0';
}

/*
 * Waits for the specified duration or until cancel_event is signaled.
 * @return true if the sleep completed without cancellation, or false if cancelled
 */
bool sleep_or_cancel(_In_ DWORD ms, _In_opt_ HANDLE cancel_event) noexcept
{
        if (cancel_event) {
                return WaitForSingleObject(cancel_event, ms) != WAIT_OBJECT_0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return true;
}

/*
 * Repeatedly invokes `probe()` until it returns a ready result (non-nullopt) or error,
 * `cancel_event` is signaled (ERROR_CANCELLED), or `deadline` expires (WAIT_TIMEOUT).
 */
template<typename Fn>
auto poll_until(
        _In_ Fn &&probe,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_ DWORD interval_ms,
        _In_opt_ HANDLE cancel_event)
        -> std::expected<typename std::invoke_result_t<Fn&>::value_type::value_type, DWORD>
{
        for (;;) {
                if (is_signaled(cancel_event)) {
                        return std::unexpected(ERROR_CANCELLED);
                }

                auto res = probe();
                if (!res) {
                        return std::unexpected(res.error());
                }

                if (auto &r = *res) {
                        return std::move(*r);
                }

                if (std::chrono::steady_clock::now() >= deadline) {
                        return std::unexpected(WAIT_TIMEOUT);
                }

                if (!sleep_or_cancel(interval_ms, cancel_event)) {
                        return std::unexpected(ERROR_CANCELLED);
                }
        }
}

/*
 * Retrieves the DEVINST handle of our VHCI USB host controller.
 * Returns USBIP_ERROR_VHCI_NOT_FOUND if the controller is not present.
 */
std::expected<DEVINST, DWORD> get_vhci_devinst()
{
        hdevinfo di(SetupDiGetClassDevs(&vhci::GUID_DEVINTERFACE_USBIP_VHCI, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

        if (!di) {
                auto err = GetLastError();
                libusbip::output("SetupDiGetClassDevs(GUID_DEVINTERFACE_USBIP_VHCI) error {}", err);
                return std::unexpected(err);
        }

        if (SP_DEVINFO_DATA dd{ .cbSize = sizeof(dd) };
            SetupDiEnumDeviceInfo(di.get(), 0, &dd)) {
                return dd.DevInst;
        }

        auto err = GetLastError();
        if (err == ERROR_NO_MORE_ITEMS) {
                err = USBIP_ERROR_VHCI_NOT_FOUND;
        }

        libusbip::output("SetupDiEnumDeviceInfo(GUID_DEVINTERFACE_USBIP_VHCI) error {}", err);
        return std::unexpected(err);
}

/*
 * Retrieves the 1-based USB hub port number (DEVPKEY_Device_Address) of a devnode.
 * Returns 0 if the property is missing, uncommitted, or invalid.
 */
int get_device_address(_In_ DEVINST devinst) noexcept
{
        if (!devinst) {
                return 0;
        }

        DEVPROPTYPE typ;
        ULONG port;
        ULONG len = sizeof(port);

        auto ret = CM_Get_DevNode_Property(devinst, &DEVPKEY_Device_Address, &typ,
                                           reinterpret_cast<BYTE*>(&port), &len, 0);

        if (ret != CR_SUCCESS) {
                return 0;
        }

        assert(typ == DEVPROP_TYPE_UINT32);
        assert(len == sizeof(port));

        return static_cast<int>(port);
}

/*
 * Traverses the devnode hierarchy from the disk up to the host controller.
 * Returns the 1-based USB hub port number (DEVPKEY_Device_Address) of the disk
 * ONLY if the disk is connected to our VHCI host controller devnode.
 *
 * Returns 0 if the disk is not on our USB port, or if properties are not yet available.
 * Non-USB disks (NVMe, SATA, etc.) and USB disks on other (physical or foreign virtual)
 * hubs fail ancestry verification.
 * For our virtual USB device, failure can occur transiently during PnP arrival before properties
 * are committed (or upon surprise removal); returning 0 allows the caller's polling loop to retry.
 *
 * @param disk_devinst Devnode instance handle of the disk
 * @param vhci_devinst Devnode instance handle of our VHCI controller
 * @return 1-based USB port number on our hub, or 0 on failure/unrelated disk
 */
auto get_disk_port_number(_In_ DEVINST disk_devinst, _In_ DEVINST vhci_devinst) noexcept
{
        for (DEVINST cur = disk_devinst, child{}, parent;
             CM_Get_Parent(&parent, cur, 0) == CR_SUCCESS; cur = parent) {

                if (parent == vhci_devinst) {
                        return get_device_address(child);
                }

                child = cur;
        }

        return 0;
}

/*
 * Retrieves the device interface path for an enumerated device interface.
 * Returns std::nullopt on failure (logged to debug output) to allow enumeration loops
 * to skip transient or inaccessible interfaces without aborting the discovery scan.
 */
std::optional<std::wstring> get_device_interface_path(_In_ HDEVINFO di, _In_ SP_DEVICE_INTERFACE_DATA &idata)
{
        DWORD req_sz{};
        [[maybe_unused]] auto ok = SetupDiGetDeviceInterfaceDetail(di, &idata, nullptr, 0, &req_sz, nullptr);
        assert(!ok);

        if (auto err = GetLastError(); err != ERROR_INSUFFICIENT_BUFFER) {
                libusbip::output("SetupDiGetDeviceInterfaceDetail error {}", err);
                return std::nullopt;
        }

        auto buf = std::make_unique_for_overwrite<BYTE[]>(req_sz);

        auto &detail = *reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(buf.get());
        detail.cbSize = sizeof(detail);

        if (!SetupDiGetDeviceInterfaceDetail(di, &idata, &detail, req_sz, nullptr, nullptr)) {
                auto err = GetLastError();
                libusbip::output("SetupDiGetDeviceInterfaceDetail error {}", err);
                return std::nullopt;
        }

        return detail.DevicePath;
}

/*
 * Opens a disk device interface and queries its physical disk number via IOCTL_STORAGE_GET_DEVICE_NUMBER.
 * Returns std::nullopt on failure (logged to debug output) to indicate the disk number could not be
 * determined, allowing callers to skip non-queryable disks without halting enumeration.
 */
std::optional<DWORD> get_storage_device_number(_In_ const std::wstring &device_path)
{
        Handle disk(CreateFile(device_path.c_str(), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));

        if (!disk) {
                auto err = GetLastError();
                libusbip::output(L"CreateFile('{}') error {}", device_path, err);
                return std::nullopt;
        }

        STORAGE_DEVICE_NUMBER sdn;
        DWORD bytes_returned; // must be set if the last arg is NULL

        if (!DeviceIoControl(disk.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                             nullptr, 0, &sdn, sizeof(sdn), &bytes_returned, nullptr)) {

                auto err = GetLastError();
                libusbip::output("DeviceIoControl(IOCTL_STORAGE_GET_DEVICE_NUMBER) error {}", err);
                return std::nullopt;
        }

        assert(bytes_returned == sizeof(sdn));
        return sdn.DeviceNumber;
}

/*
 * Enumerates the GUID_DEVINTERFACE_DISK interface for a devnode and obtains its physical disk number.
 * Returns std::nullopt on failure (logged to debug output); callers ignore the error to continue
 * scanning other disks on the system without aborting the port disk list.
 */
std::optional<DWORD> get_disk_number(_In_ HDEVINFO di, _In_ SP_DEVINFO_DATA &dd)
{
        SP_DEVICE_INTERFACE_DATA idata { .cbSize = sizeof(idata) };

        if (!SetupDiEnumDeviceInterfaces(di, &dd, &GUID_DEVINTERFACE_DISK, 0, &idata)) {
                auto err = GetLastError();
                libusbip::output("SetupDiEnumDeviceInterfaces(GUID_DEVINTERFACE_DISK) error {}", err);
                return std::nullopt;
        }

        if (auto path = get_device_interface_path(di, idata)) {
                return get_storage_device_number(*path);
        }

        return std::nullopt;
}

std::expected<std::vector<DWORD>, DWORD> get_disks_for_port(_In_ DEVINST vhci_devinst, _In_ int port)
{
        assert(port > 0);

        hdevinfo di(SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

        if (!di) {
                auto err = GetLastError();
                libusbip::output("SetupDiGetClassDevs(GUID_DEVINTERFACE_DISK) error {}", err);
                return std::unexpected(err);
        }

        std::vector<DWORD> disk_numbers;

        for (DWORD i = 0; ; ++i) {

                SP_DEVINFO_DATA dd { .cbSize = sizeof(dd) };

                if (!SetupDiEnumDeviceInfo(di.get(), i, &dd)) {
                        if (auto err = GetLastError(); err != ERROR_NO_MORE_ITEMS) {
                                libusbip::output("SetupDiEnumDeviceInfo error {}", err);
                                return std::unexpected(err);
                        }
                        break;
                }

                if (get_disk_port_number(dd.DevInst, vhci_devinst) == port) {
                        if (auto num = get_disk_number(di.get(), dd)) {
                                disk_numbers.push_back(*num);
                        }
                }
        }

        return disk_numbers;
}

DWORD query_disk_extents(
        _In_ HANDLE vol,
        _Out_writes_bytes_(sz) VOLUME_DISK_EXTENTS *buf, _In_ DWORD sz,
        _Out_ DWORD &bytes_returned, _In_ bool retry_on_more_data = false) noexcept
{
        if (DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf, sz, &bytes_returned, nullptr)) {
                return ERROR_SUCCESS;
        }

        auto err = GetLastError();
        if (!(err == ERROR_MORE_DATA && retry_on_more_data)) {
                libusbip::output("DeviceIoControl(IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS) error {}", err);
        }
        return err;
}

/*
 * Retrieves the physical disk extents spanned by a volume via IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS.
 * Returns std::nullopt on failure (logged to debug output). Non-disk, raw, or virtual volumes
 * naturally fail this query and are skipped as non-matching without propagating an error code.
 */
std::optional<std::vector<DISK_EXTENT>> get_volume_disk_extents(_In_ HANDLE vol)
{
        VOLUME_DISK_EXTENTS extents{};

        const auto &cnt = extents.NumberOfDiskExtents;
        static_assert(!offsetof(VOLUME_DISK_EXTENTS, NumberOfDiskExtents));

        DWORD bytes_returned{};
        auto err = query_disk_extents(vol, &extents, sizeof(extents), bytes_returned, true);

        if (err == ERROR_SUCCESS) {
                if (bytes_returned == sizeof(extents) && cnt == 1) {
                        return std::vector<DISK_EXTENT>(extents.Extents, extents.Extents + 1);
                }
                return std::nullopt;
        }

        if (!(err == ERROR_MORE_DATA &&
              bytes_returned >= sizeof(cnt) && // is a first member, see static_assert above
              cnt > 1 && cnt <= 64)) { // arbitrary upper limit, matches drivers/ude/isolation.cpp
                return std::nullopt;
        }

        const auto req_sz = sizeof_anysize_struct(VOLUME_DISK_EXTENTS, Extents, cnt);

        auto buf = std::make_unique_for_overwrite<char[]>(req_sz);
        auto pextents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buf.get());

        if (query_disk_extents(vol, pextents, static_cast<DWORD>(req_sz), bytes_returned) != ERROR_SUCCESS) {
                return std::nullopt;
        }

        if (const auto &n = pextents->NumberOfDiskExtents;
            bytes_returned >= sizeof(*pextents) &&
            n && n <= cnt && // 1 is OK, can shrink between the two DeviceIoControl calls
            bytes_returned >= sizeof_anysize_struct(VOLUME_DISK_EXTENTS, Extents, n)) {

                return std::vector<DISK_EXTENT>(pextents->Extents, pextents->Extents + n);
        }

        return std::nullopt;
}

/*
 * Evaluates whether a volume interface belongs to any of the specified disks and resolves its NT path.
 * Returns std::nullopt if the volume cannot be opened, has no extents on the target disks, or cannot
 * be resolved. Acts as a filtering projection where non-matching/inaccessible volumes are skipped.
 */
std::optional<std::wstring> get_volume_nt_device(_In_ const std::wstring &path, _In_ const std::vector<DWORD> &disk_numbers)
{
        Handle vol(CreateFile(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!vol) {
                auto err = GetLastError();
                libusbip::output(L"CreateFile('{}') error {}", path, err);
                return std::nullopt;
        }

        auto extents = get_volume_disk_extents(vol.get());
        if (!extents) {
                return std::nullopt;
        }

        auto pred = [&disk_numbers] (const auto &extent)
        {
                return std::ranges::contains(disk_numbers, extent.DiskNumber);
        };

        if (std::ranges::none_of(*extents, std::move(pred))) {
                return std::nullopt;
        }

        return resolve_volume_device(path);
}

std::expected<std::vector<std::wstring>, DWORD> get_volumes_for_disks(_In_ const std::vector<DWORD> &disk_numbers)
{
        std::vector<std::wstring> volumes;
        if (disk_numbers.empty()) {
                return volumes;
        }

        hdevinfo di(SetupDiGetClassDevs(&GUID_DEVINTERFACE_VOLUME, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

        if (!di) {
                auto err = GetLastError();
                libusbip::output("SetupDiGetClassDevs(GUID_DEVINTERFACE_VOLUME) error {}", err);
                return std::unexpected(err);
        }

        for (DWORD i = 0; ; ++i) {

                SP_DEVICE_INTERFACE_DATA idata { .cbSize = sizeof(idata) };

                if (!SetupDiEnumDeviceInterfaces(di.get(), nullptr, &GUID_DEVINTERFACE_VOLUME, i, &idata)) {
                        if (auto err = GetLastError(); err != ERROR_NO_MORE_ITEMS) {
                                libusbip::output("SetupDiEnumDeviceInterfaces(GUID_DEVINTERFACE_VOLUME) error {}", err);
                                return std::unexpected(err);
                        }
                        break;
                }

                auto path = get_device_interface_path(di.get(), idata);
                if (!path) {
                        continue;
                }

                if (auto nt_dev = get_volume_nt_device(*path, disk_numbers)) {
                        volumes.push_back(std::move(*nt_dev));
                }
        }

        return volumes;
}

bool remove_drive_letter(_In_ wchar_t drive_letter, _In_ const std::wstring &nt_device)
{
        const DWORD flags = DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH;
        const wchar_t drive[] { drive_letter, L':', L'\0' };

        if (!DefineDosDevice(flags, drive, nt_device.c_str())) {
                auto err = GetLastError();
                libusbip::output(L"DefineDosDevice(remove '{}' -> '{}') error {}", drive, nt_device, err);
                return false;
        }

        return true;
}

DWORD isolate_volume_kernel(
        _In_ HANDLE vhci, _In_ int port, _In_ const std::wstring &nt_device, _Inout_ wchar_t &drive_letter) noexcept
{
        vhci::ioctl::isolate_port_volume req {
                .drive_letter = drive_letter,
                .port = port,
        };
        req.size = sizeof(req);

        if (auto hr = StringCchCopy(req.nt_device, std::size(req.nt_device), nt_device.c_str()); FAILED(hr)) {
                libusbip::output("StringCchCopy error {:#x}", static_cast<unsigned long>(hr));
                return win32_from_hresult(hr);
        }

        constexpr auto outlen = req.output_size();

        if (DWORD bytes_returned; // must be set if the last arg is NULL
            !DeviceIoControl(vhci, vhci::ioctl::ISOLATE_PORT_VOLUME,
                             &req, sizeof(req), &req, outlen, &bytes_returned, nullptr)) {

                auto err = GetLastError();
                libusbip::output(L"DeviceIoControl(ISOLATE_PORT_VOLUME, port={}, '{}') error {}", port, nt_device, err);
                return err;
        } else if (!drive_letter && bytes_returned >= outlen) {
                drive_letter = req.drive_letter;
        }

        return ERROR_SUCCESS;
}

bool define_session_drive_letter(_In_ wchar_t drive_letter, _In_ const std::wstring &nt_device)
{
        const wchar_t drive[] { drive_letter, L':', L'\0' };

        if (!DefineDosDevice(DDD_RAW_TARGET_PATH, drive, nt_device.c_str())) {
                auto err = GetLastError();
                libusbip::output(L"DefineDosDevice('{}' -> '{}') error {}", drive, nt_device, err);
                return false;
        }

        return true;
}

std::optional<isolated_volume> isolate_volume(
        _In_ HANDLE vhci_dev,
        _In_ int port,
        _In_ const std::wstring &nt_device,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event = HANDLE{})
{
        if (check_cancelled(cancel_event)) {
                return std::nullopt;
        }

        auto drive_letter = find_volume_drive_letter(nt_device);
        auto err = isolate_volume_kernel(vhci_dev, port, nt_device, drive_letter);

        if (err == ERROR_SUCCESS && !drive_letter) {
                const auto letter_deadline = std::min(
                        deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(1000));

                auto probe = [vhci_dev, port, &nt_device, &err]
                        -> std::expected<std::optional<wchar_t>, DWORD>
                {
                        auto letter = find_volume_drive_letter(nt_device);
                        err = isolate_volume_kernel(vhci_dev, port, nt_device, letter);
                        if (err != ERROR_SUCCESS) {
                                return std::unexpected(err);
                        }
                        if (letter) {
                                return letter;
                        }
                        return std::nullopt;
                };

                auto res = poll_until(probe, letter_deadline, 50, cancel_event);
                if (res) {
                        drive_letter = *res;
                } else if (res.error() == ERROR_CANCELLED) {
                        return std::nullopt;
                }
        }

        if (check_cancelled(cancel_event)) {
                return std::nullopt;
        }

        if (drive_letter && !define_session_drive_letter(drive_letter, nt_device)) {
                drive_letter = L'\0';
        }

        return isolated_volume {
                .nt_device = nt_device,
                .drive_letter = drive_letter,
                .dacl_error = err
        };
}

std::expected<std::vector<DWORD>, DWORD> wait_for_port_disks(
        _In_ HANDLE vhci, _In_ int port,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event)
{
        auto vhci_devinst = get_vhci_devinst();
        if (!vhci_devinst) {
                return std::unexpected(vhci_devinst.error());
        }

        auto probe = [vhci, dev = *vhci_devinst, port] -> std::expected<std::optional<std::vector<DWORD>>, DWORD>
        {
                if (auto disks = get_disks_for_port(dev, port); !disks) {
                        return std::unexpected(disks.error());
                } else if (!disks->empty()) {
                        return std::move(*disks);
                }

                if (auto st = vhci::get_port_storage(vhci, port); !st) {
                        return std::unexpected(st.error());
                } else if (*st != vhci::port_storage::maybe_storage) {
                        libusbip::output("Port {} device is {}; skipping volume wait", port,
                                         *st == vhci::port_storage::non_storage ? "non-storage" : "detached");
                        return std::vector<DWORD>{};
                }

                return std::nullopt;
        };

        return poll_until(probe, deadline, 200, cancel_event);
}

std::expected<std::vector<std::wstring>, DWORD> wait_for_disk_volumes(
        _In_ const std::vector<DWORD> &disks,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event)
{
        if (disks.empty()) {
                return std::vector<std::wstring>{};
        }

        auto probe = [&disks]() -> std::expected<std::optional<std::vector<std::wstring>>, DWORD>
        {
                auto res = get_volumes_for_disks(disks);
                if (!res) {
                        return std::unexpected(res.error());
                }

                if (!res->empty()) {
                        return std::move(*res);
                }

                return std::nullopt;
        };

        return poll_until(probe, deadline, 200, cancel_event);
}

std::expected<std::vector<std::wstring>, DWORD> wait_for_port_volumes(
        _In_ HANDLE vhci, _In_ int port,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event)
{
        if (auto disks = wait_for_port_disks(vhci, port, deadline, cancel_event)) {
                return wait_for_disk_volumes(*disks, deadline, cancel_event);
        } else {
                return std::unexpected(disks.error());
        }
}

} // namespace


/*
 * Discovers and isolates storage volumes associated with a USB port.
 *
 * Should be called by the client application immediately after vhci::attach()
 * returns a valid port (> 0), provided that isolation mode is enabled
 * (config.iso_mode != isolation::none). If the attached device is non-storage
 * or isolation is not requested, this function should not be called.
 *
 * Privileged volume isolation (\GLOBAL?? drive letter removal and volume/root DACL
 * hardening) is performed in kernel mode by the VHCI driver via ISOLATE_PORT_VOLUME
 * (in addition to automatic kernel PnP volume arrival DACL hardening), so standard
 * non-admin users receive full isolation while this function creates the session-local
 * DosDevices drive letter in the caller's interactive session.
 */
auto usbip::isolate_port_volumes(
        _In_ HANDLE dev,
        _In_ int port,
        _In_ unsigned long timeout_ms,
        _In_opt_ HANDLE cancel_event) -> std::optional<std::vector<isolated_volume>>
{
        if (port <= 0) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return std::nullopt;
        }

        if (check_cancelled(cancel_event)) {
                return std::nullopt;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        auto vols = wait_for_port_volumes(dev, port, deadline, cancel_event);
        if (!vols) {
                SetLastError(vols.error());
                return std::nullopt;
        }

        if (vols->empty()) {
                return std::vector<isolated_volume>{};
        }

        std::vector<isolated_volume> volumes;
        volumes.reserve(vols->size());

        for (const auto &nt_device: *vols) {
                if (auto vol = isolate_volume(dev, port, nt_device, deadline, cancel_event)) {
                        volumes.push_back(std::move(*vol));
                } else {
                        return std::nullopt;
                }
        }

        return volumes;
}

std::optional<std::vector<usbip::isolated_volume>> usbip::isolate_port_volumes(
        _In_ HANDLE dev, _In_ int port, _In_ unsigned long timeout_ms, _In_ std::stop_token stoken)
{
        if (auto evt = stop_event(stoken)) {
                return isolate_port_volumes(dev, port, timeout_ms, evt.get());
        }

        return std::nullopt;
}

bool usbip::cleanup_port_volumes(_In_ int port)
{
        if (port <= 0) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
        }

        auto vhci_devinst = get_vhci_devinst();
        if (!vhci_devinst) {
                SetLastError(vhci_devinst.error());
                return false;
        }

        auto disks = get_disks_for_port(*vhci_devinst, port);
        if (!disks) {
                SetLastError(disks.error());
                return false;
        }

        if (disks->empty()) {
                return true;
        }

        auto volumes = get_volumes_for_disks(*disks);
        if (!volumes) {
                SetLastError(volumes.error());
                return false;
        }

        for (const auto &nt_device: *volumes) {
                if (auto letter = find_volume_drive_letter(nt_device)) {
                        remove_drive_letter(letter, nt_device);
                }
        }

        return true;
}
