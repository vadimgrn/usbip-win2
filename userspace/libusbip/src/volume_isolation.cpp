/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../volume_isolation.h"
#include "../win_handle.h"

#include "sid.h"
#include "output.h"
#include "setupapi.h"

#include <resources/messages.h>

#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <sddl.h>
#include <aclapi.h>

#include <thread>
#include <algorithm>

namespace
{

using namespace usbip;

struct async_isolation_context
{
        int port{};
        unsigned long timeout_ms{};
        HANDLE cancel_event{};
        isolate_port_volumes_callback callback;
};

/*
 * Creates a security descriptor with a protected DACL granting full access (GA)
 * to SYSTEM (SY), the caller's user account, and the current logon session (if present).
 * Administrators (BA) is intentionally omitted to deny access to foreign admins.
 * Uses OICI inheritance flags so newly created files and subdirectories inherit permissions.
 *
 * @return HLocal owning the allocated SECURITY_DESCRIPTOR, or Win32 error code on failure
 */
std::expected<HLocal, DWORD> make_hardened_user_sd()
{
        auto caller_aces = make_caller_aces(L"OICI");
        if (!caller_aces) {
                return std::unexpected(caller_aces.error());
        }

        auto sddl = L"D:P(A;OICI;GA;;;SY)" + *caller_aces;

        if (PSECURITY_DESCRIPTOR sd;
            ConvertStringSecurityDescriptorToSecurityDescriptor(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
                return HLocal(sd);
        }

        auto err = GetLastError();
        libusbip::output("ConvertStringSecurityDescriptorToSecurityDescriptor error {}", err);
        return std::unexpected(err);
}

/*
 * Hardens the volume DACL to allow only SYSTEM and the caller's session/user.
 *
 * Secures both layers:
 * 1. Kernel device object (SE_KERNEL_OBJECT): vol_path (e.g. \\?\Volume{guid})
 *    is a raw DASD handle to \Device\HarddiskVolumeN; protects against raw sector
 *    access, dismount, and format.
 * 2. File system root directory (SE_FILE_OBJECT): drive_letter (e.g. X:\)
 *    is a Win32 file path; protects file system browsing and file access.
 */
auto harden_volume_dacl(_In_ const std::wstring &vol_path, _In_ wchar_t drive_letter)
{
        auto sd = make_hardened_user_sd();
        if (!sd) {
                return sd.error();
        }

        ACL *dacl;
        BOOL dacl_present;
        BOOL dacl_defaulted; // ignored: SD was explicitly constructed, not inherited or defaulted
        if (!GetSecurityDescriptorDacl(sd->get(), &dacl_present, &dacl, &dacl_defaulted)) {
                auto err = GetLastError();
                libusbip::output("GetSecurityDescriptorDacl error {}", err);
                return err;
        }

        if (!(dacl_present && dacl)) { // a NULL DACL grants unrestricted access to everyone
                return static_cast<DWORD>(ERROR_INVALID_SECURITY_DESCR);
        }

        Handle vol(CreateFile(vol_path.c_str(), READ_CONTROL | WRITE_DAC,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));

        if (!vol) {
                auto err = GetLastError();
                libusbip::output(L"CreateFile('{}') error {}", vol_path, err);
                return err;
        }

        constexpr SECURITY_INFORMATION sinfo = DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;
        auto ret = SetSecurityInfo(vol.get(), SE_KERNEL_OBJECT, sinfo, nullptr, nullptr, dacl, nullptr);

        if (ret != ERROR_SUCCESS) {
                libusbip::output(L"SetSecurityInfo('{}') error {}", vol_path, ret);
        } else if (drive_letter) {
                wchar_t root[] { drive_letter, L':', L'\\', L'\0' };
                DWORD flags{};
                if (GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) &&
                    !(flags & FILE_PERSISTENT_ACLS)) {
                        libusbip::output(L"Filesystem on '{}' does not support persistent ACLs, skipping root DACL", root);
                } else {
                        auto err = SetNamedSecurityInfo(root, SE_FILE_OBJECT, sinfo, nullptr, nullptr, dacl, nullptr);
                        if (err != ERROR_SUCCESS) {
                                if (err == ERROR_NOT_SUPPORTED || err == ERROR_INVALID_FUNCTION) {
                                        libusbip::output(L"SetNamedSecurityInfo('{}') not supported on filesystem (error {})", root, err);
                                } else {
                                        libusbip::output(L"SetNamedSecurityInfo('{}') error {}", root, err);
                                        ret = err;
                                }
                        }
                }
        }

        return ret;
}

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
 * @param vol_guid_path volume GUID path
 * @see to_dos_device_name
 */
std::optional<std::wstring> resolve_volume_device(_In_ std::wstring_view vol_guid_path)
{
        std::wstring name(to_dos_device_name(vol_guid_path));

        if (wchar_t target[MAX_PATH]; QueryDosDevice(name.c_str(), target, std::size(target))) {
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

                if (wchar_t target[MAX_PATH]; QueryDosDevice(drive, target, std::size(target))) {
                        if (equal_ordinal(target, nt_device, /*ignore_case=*/true)) {
                                return ch;
                        }
                }
        }

        return L'\0';
}

/*
 * Waits for MountMgr to assign a drive letter to the volume, polling until a letter
 * is found or the deadline is reached. Bounded by a reasonable wait (up to 1 second)
 * to avoid delaying when the volume is unformatted, unassigned, or an EFI system partition.
 */
/*
 * Waits for the specified duration or until cancel_event is signaled.
 * Returns true if the sleep completed without cancellation, or false if cancelled.
 */
bool sleep_or_cancel(_In_ unsigned long ms, _In_opt_ HANDLE cancel_event) noexcept
{
        if (cancel_event) {
                return WaitForSingleObject(cancel_event, ms) != WAIT_OBJECT_0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return true;
}

bool is_cancelled(_In_opt_ HANDLE cancel_event) noexcept
{
        return cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0;
}

wchar_t wait_for_volume_drive_letter(
        _In_ std::wstring_view nt_device,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event = HANDLE{})
{
        const auto letter_deadline = std::min(
                deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(1000));

        while (true) {
                if (is_cancelled(cancel_event)) {
                        break;
                }

                if (auto letter = find_volume_drive_letter(nt_device)) {
                        return letter;
                }

                if (std::chrono::steady_clock::now() >= letter_deadline) {
                        break;
                }

                if (!sleep_or_cancel(50, cancel_event)) {
                        break;
                }
        }

        return L'\0';
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

        DEVPROPTYPE typ{};
        ULONG port{};
        ULONG len = sizeof(port);

        auto ret = CM_Get_DevNode_Property(devinst, &DEVPKEY_Device_Address, &typ,
                                            reinterpret_cast<BYTE*>(&port), &len, 0);

        if (ret != CR_SUCCESS || typ != DEVPROP_TYPE_UINT32 || len != sizeof(port)) {
                return 0;
        }

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
 * Finds the direct USB device devnode attached to the given roothub port.
 * Returns 0 if not yet enumerated or not found.
 */
DEVINST get_port_usb_device(_In_ DEVINST vhci_devinst, _In_ int port) noexcept
{
        DEVINST root_hub;
        if (CM_Get_Child(&root_hub, vhci_devinst, 0) != CR_SUCCESS) {
                return 0;
        }

        DEVINST child;
        if (CM_Get_Child(&child, root_hub, 0) != CR_SUCCESS) {
                return 0;
        }

        do {
                if (get_device_address(child) == port) {
                        return child;
                }
        } while (CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);

        return 0;
}

/*
 * Helper to query a string property from a devnode.
 */
std::wstring get_devnode_string_property(_In_ DEVINST devinst, _In_ const DEVPROPKEY &key)
{
        DEVPROPTYPE typ{};
        ULONG len = 0;
        if (CM_Get_DevNode_Property(devinst, &key, &typ, nullptr, &len, 0) != CR_BUFFER_SMALL || !len) {
                return {};
        }

        std::wstring val(len / sizeof(wchar_t), L'\0');
        if (CM_Get_DevNode_Property(devinst, &key, &typ, reinterpret_cast<BYTE*>(val.data()), &len, 0) != CR_SUCCESS) {
                return {};
        }

        while (!val.empty() && val.back() == L'\0') {
                val.pop_back();
        }
        return val;
}

/*
 * Determines if a devnode attached to a port is conclusively a non-storage device
 * (e.g. HID, audio, camera, bluetooth, etc.) to skip unnecessary volume arrival polling delays.
 */
bool is_non_storage_device(_In_ DEVINST devinst)
{
        auto dev_class = get_devnode_string_property(devinst, DEVPKEY_Device_Class);
        if (dev_class.empty()) {
                return false;
        }

        constexpr std::wstring_view non_storage_classes[] = {
                L"HIDClass", L"Media", L"Net", L"Bluetooth", L"Ports",
                L"SmartCardReader", L"Camera", L"Image", L"PrintQueue",
                L"Printer", L"Keyboard", L"Mouse", L"Biometric"
        };

        for (auto cls : non_storage_classes) {
                if (equal_ordinal(dev_class, cls, true)) {
                        return true;
                }
        }

        auto service = get_devnode_string_property(devinst, DEVPKEY_Device_Service);
        constexpr std::wstring_view non_storage_services[] = {
                L"HidUsb", L"usbaudio", L"usbvideo", L"BthUSB", L"usbprint",
                L"kbdhid", L"mouhid"
        };

        for (auto s : non_storage_services) {
                if (equal_ordinal(service, s, true)) {
                        return true;
                }
        }

        return false;
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

        DWORD actual{};
        STORAGE_DEVICE_NUMBER sdn{};
        if (!DeviceIoControl(disk.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                             nullptr, 0, &sdn, sizeof(sdn), &actual, nullptr) ||
            actual != sizeof(sdn)) {
                auto err = GetLastError();
                libusbip::output("DeviceIoControl(IOCTL_STORAGE_GET_DEVICE_NUMBER) error {}", err);
                return std::nullopt;
        }

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

                auto dev_port = get_disk_port_number(dd.DevInst, vhci_devinst);
                if (port <= 0 ? dev_port > 0 : dev_port == port) {
                        if (auto num = get_disk_number(di.get(), dd)) {
                                disk_numbers.push_back(*num);
                        }
                }
        }

        return disk_numbers;
}

/*
 * Retrieves the physical disk extents spanned by a volume via IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS.
 * Returns std::nullopt on failure (logged to debug output). Non-disk, raw, or virtual volumes
 * naturally fail this query and are skipped as non-matching without propagating an error code.
 */
std::optional<std::vector<DISK_EXTENT>> get_volume_disk_extents(_In_ HANDLE vol)
{
        VOLUME_DISK_EXTENTS extents{};

        if (DWORD ret{}; DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                         nullptr, 0, &extents, sizeof(extents), &ret, nullptr)) {
                return std::vector<DISK_EXTENT>(extents.Extents, extents.Extents + extents.NumberOfDiskExtents);
        }

        if (auto err = GetLastError(); err != ERROR_MORE_DATA) {
                libusbip::output("DeviceIoControl(IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS) error {}", err);
                return std::nullopt;
        }

        if (extents.NumberOfDiskExtents <= 1) {
                return std::nullopt;
        }

        const auto req_sz = static_cast<DWORD>(
                sizeof(extents) + (extents.NumberOfDiskExtents - 1)*sizeof(*extents.Extents));

        auto buf = std::make_unique_for_overwrite<char[]>(req_sz);
        auto pextents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buf.get());

        if (DWORD ret{}; !DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                          nullptr, 0, pextents, req_sz, &ret, nullptr)) {
                auto err = GetLastError();
                libusbip::output("DeviceIoControl(IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS) error {}", err);
                return std::nullopt;
        }

        const auto count = std::min(pextents->NumberOfDiskExtents, extents.NumberOfDiskExtents);
        return std::vector<DISK_EXTENT>(pextents->Extents, pextents->Extents + count);
}

struct volume_info
{
        std::wstring vol_guid_path;
        std::wstring nt_device;
        DWORD disk_number{};
};

/*
 * Evaluates whether a volume interface belongs to any of the specified disks and resolves its NT path.
 * Returns std::nullopt if the volume cannot be opened, has no extents on the target disks, or cannot
 * be resolved. Acts as a filtering projection where non-matching/inaccessible volumes are skipped.
 */
std::optional<volume_info> get_volume_info(_In_ std::wstring path, _In_ const std::vector<DWORD> &disk_numbers)
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

        auto it = std::ranges::find_if(*extents, pred);
        if (it == extents->end()) {
                return std::nullopt;
        }

        auto nt_dev = resolve_volume_device(path);
        if (!nt_dev) {
                return std::nullopt;
        }

        return volume_info {
                .vol_guid_path = std::move(path),
                .nt_device = std::move(*nt_dev),
                .disk_number = it->DiskNumber,
        };
}

std::expected<std::vector<volume_info>, DWORD> get_volumes_for_disks(_In_ const std::vector<DWORD> &disk_numbers)
{
        std::vector<volume_info> volumes;
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

                if (auto vi = get_volume_info(std::move(*path), disk_numbers)) {
                        volumes.push_back(std::move(*vi));
                }
        }

        return volumes;
}

bool remove_drive_letter(_In_ wchar_t drive_letter, _In_ const std::wstring &nt_device)
{
        const wchar_t drive[] { drive_letter, L':', L'\0' };

        if (const DWORD flags = DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH;
            DefineDosDevice(flags, drive, nt_device.c_str())) {
                return true;
        }

        auto err = GetLastError();
        libusbip::output(L"DefineDosDevice(remove '{}' -> '{}') error {}", drive, nt_device, err);
        return false;
}

bool isolate_drive_letter(_In_ wchar_t drive_letter, _In_ const std::wstring &nt_device)
{
        const wchar_t drive[] { drive_letter, L':', L'\0' };
        if (!remove_drive_letter(drive_letter, nt_device)) {
                libusbip::output(L"Warning: could not remove global drive letter '{}' from \\GLOBAL??; "
                                 L"isolation from other sessions may be incomplete", drive);
        }

        if (!DefineDosDevice(DDD_RAW_TARGET_PATH, drive, nt_device.c_str())) {
                auto err = GetLastError();
                libusbip::output(L"DefineDosDevice('{}' -> '{}') error {}", drive, nt_device, err);
                return false;
        }

        return true;
}

auto isolate_volume(
        _In_ const volume_info &v,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event = HANDLE{})
{
        auto drive_letter = wait_for_volume_drive_letter(v.nt_device, deadline, cancel_event);

        if (drive_letter && !isolate_drive_letter(drive_letter, v.nt_device)) {
                drive_letter = L'\0';
        }

        return isolated_volume {
                .nt_device = v.nt_device,
                .drive_letter = drive_letter,
                .dacl_error = harden_volume_dacl(v.vol_guid_path, drive_letter)
        };
}

std::expected<std::vector<DWORD>, DWORD> wait_for_port_disks(
        _In_ int port, _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event)
{
        auto vhci_devinst = get_vhci_devinst();
        if (!vhci_devinst) {
                return std::unexpected(vhci_devinst.error());
        }

        while (true) {
                if (is_cancelled(cancel_event)) {
                        return std::unexpected(static_cast<DWORD>(ERROR_CANCELLED));
                }

                auto v = get_disks_for_port(*vhci_devinst, port);
                if (!v) {
                        return std::unexpected(v.error());
                }

                if (!v->empty()) {
                        return std::move(*v);
                }

                if (auto usb_dev = get_port_usb_device(*vhci_devinst, port);
                    usb_dev && is_non_storage_device(usb_dev)) {
                        libusbip::output("Port {} device is non-storage; skipping volume wait", port);
                        return std::vector<DWORD>{};
                }

                if (std::chrono::steady_clock::now() >= deadline) {
                        return std::unexpected(static_cast<DWORD>(WAIT_TIMEOUT));
                }

                if (!sleep_or_cancel(200, cancel_event)) {
                        return std::unexpected(static_cast<DWORD>(ERROR_CANCELLED));
                }
        }
}

std::expected<std::vector<volume_info>, DWORD> wait_for_disk_volumes(
        _In_ const std::vector<DWORD> &disks,
        _In_ std::chrono::steady_clock::time_point deadline,
        _In_opt_ HANDLE cancel_event)
{
        if (disks.empty()) {
                return std::vector<volume_info>{};
        }

        while (true) {
                if (is_cancelled(cancel_event)) {
                        return std::unexpected(static_cast<DWORD>(ERROR_CANCELLED));
                }

                auto res = get_volumes_for_disks(disks);
                if (!res) {
                        return std::unexpected(res.error());
                }

                if (!res->empty()) {
                        return std::move(*res);
                }

                if (std::chrono::steady_clock::now() >= deadline) {
                        return std::unexpected(static_cast<DWORD>(WAIT_TIMEOUT));
                }

                if (!sleep_or_cancel(200, cancel_event)) {
                        return std::unexpected(static_cast<DWORD>(ERROR_CANCELLED));
                }
        }
}

std::expected<std::vector<volume_info>, DWORD> wait_for_port_volumes(
        _In_ int port, _In_ unsigned long timeout_ms, _In_opt_ HANDLE cancel_event)
{
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        auto disks = wait_for_port_disks(port, deadline, cancel_event);
        if (!disks) {
                return std::unexpected(disks.error());
        }

        return wait_for_disk_volumes(*disks, deadline, cancel_event);
}

} // namespace


auto usbip::isolate_port_volumes(
        _In_ int port,
        _In_ unsigned long timeout_ms,
        _In_opt_ HANDLE cancel_event) ->
        std::optional<std::vector<isolated_volume>>
{
        if (is_cancelled(cancel_event)) {
                SetLastError(ERROR_CANCELLED);
                return std::nullopt;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        auto vols = wait_for_port_volumes(port, timeout_ms, cancel_event);
        if (!vols) {
                SetLastError(vols.error());
                return std::nullopt;
        }

        std::vector<isolated_volume> volumes;
        volumes.reserve(vols->size());

        for (const auto &v: *vols) {
                if (is_cancelled(cancel_event)) {
                        SetLastError(ERROR_CANCELLED);
                        return std::nullopt;
                }
                volumes.push_back(isolate_volume(v, deadline, cancel_event));
        }

        return volumes;
}

std::optional<std::vector<usbip::isolated_volume>> usbip::isolate_port_volumes(
        _In_ int port,
        _In_ unsigned long timeout_ms,
        _In_ std::stop_token stoken)
{
        if (stoken.stop_requested()) {
                SetLastError(ERROR_CANCELLED);
                return std::nullopt;
        }

        NullableHandle cancel_evt;
        if (stoken.stop_possible()) {
                cancel_evt.reset(CreateEvent(nullptr, true, false, nullptr));
                if (!cancel_evt) {
                        return std::nullopt;
                }
        }

        std::optional<std::stop_callback<std::function<void()>>> stop_cb;
        if (cancel_evt) {
                stop_cb.emplace(stoken, [h = cancel_evt.get()] { SetEvent(h); });
        }

        return isolate_port_volumes(port, timeout_ms, cancel_evt.get());
}

bool usbip::cleanup_port_volumes(_In_ int port)
{
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

        for (const auto &v: *volumes) {
                if (auto letter = find_volume_drive_letter(v.nt_device)) {
                        remove_drive_letter(letter, v.nt_device);
                }
        }

        return true;
}


bool usbip::isolate_port_volumes_async(
        _In_ int port,
        _In_ isolate_port_volumes_callback callback,
        _In_ unsigned long timeout_ms,
        _In_opt_ HANDLE cancel_event)
{
        if (!callback || port <= 0) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
        }

        if (is_cancelled(cancel_event)) {
                SetLastError(ERROR_CANCELLED);
                return false;
        }

        auto ctx = std::make_unique<async_isolation_context>(async_isolation_context {
                .port = port,
                .timeout_ms = timeout_ms,
                .cancel_event = cancel_event,
                .callback = std::move(callback)
        });

        auto ok = TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, PVOID param) noexcept {
                auto ctx = std::unique_ptr<async_isolation_context>(
                        static_cast<async_isolation_context*>(param));

                auto result = isolate_port_volumes(ctx->port, ctx->timeout_ms, ctx->cancel_event);

                try {
                        ctx->callback(ctx->port, std::move(result));
                } catch (...) {
                }
        }, ctx.get(), nullptr);

        if (ok) {
                ctx.release();
                return true;
        }

        return false;
}

bool usbip::isolate_port_volumes_async(
        _In_ int port,
        _In_ isolate_port_volumes_callback callback,
        _In_ unsigned long timeout_ms,
        _In_ std::stop_token stoken)
{
        if (stoken.stop_requested()) {
                SetLastError(ERROR_CANCELLED);
                return false;
        }

        if (!stoken.stop_possible()) {
                return isolate_port_volumes_async(port, std::move(callback), timeout_ms, HANDLE{});
        }

        NullableHandle cancel_evt(CreateEvent(nullptr, true, false, nullptr));
        if (!cancel_evt) {
                return false;
        }

        struct async_stop_guard
        {
                NullableHandle evt;
                std::optional<std::stop_callback<std::function<void()>>> stop_cb;
        };

        auto guard = std::make_shared<async_stop_guard>();
        guard->evt = std::move(cancel_evt);
        guard->stop_cb.emplace(stoken, [h = guard->evt.get()] { SetEvent(h); });

        auto wrapped_cb = [callback = std::move(callback), guard](
                int port, std::optional<std::vector<isolated_volume>> res) {
                callback(port, std::move(res));
        };

        return isolate_port_volumes_async(port, std::move(wrapped_cb), timeout_ms, guard->evt.get());
}
