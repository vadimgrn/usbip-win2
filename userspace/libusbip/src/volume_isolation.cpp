/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../volume_isolation.h"
#include "../format_message.h"
#include "../win_handle.h"

#include "sid.h"
#include "setupapi.h"
#include "output.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <sddl.h>
#include <aclapi.h>
#include <winioctl.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

namespace
{

using namespace usbip;

// GUID_DEVINTERFACE_DISK = {53f56307-b6bf-11d0-94f2-00a0c91efb8b}
DEFINE_GUID(GUID_DEVINTERFACE_DISK_LOCAL,
        0x53f56307, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

// GUID_DEVINTERFACE_VOLUME = {53f5630d-b6bf-11d0-94f2-00a0c91efb8b}
DEFINE_GUID(GUID_DEVINTERFACE_VOLUME_LOCAL,
        0x53f5630d, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);


std::expected<HLocal, DWORD> get_hardened_user_sd()
{
        auto ussd = get_user_ssd();
        if (!ussd) {
                return std::unexpected(ussd.error());
        }
        auto ssd = L"D:P(A;;GA;;;SY)" + *ussd;

        PSECURITY_DESCRIPTOR sd;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptor(ssd.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
                return std::unexpected(GetLastError());
        }
        return HLocal(sd);
}

/*
 * Protect DACL, allow only SYSTEM (SY) and current session/user.
 * Administrators (BA) is intentionally omitted to deny access to foreign admins.
 */
auto harden_volume_dacl(_In_ const std::wstring &vol_path, _In_opt_ wchar_t drive_letter)
{
        auto sd = get_hardened_user_sd();
        if (!sd) {
                return sd.error();
        }

        ACL *dacl;
        BOOL dacl_present;
        BOOL dacl_defaulted;
        if (!GetSecurityDescriptorDacl(sd->get(), &dacl_present, &dacl, &dacl_defaulted)) {
                return GetLastError();
        }

        Handle vol(CreateFile(vol_path.c_str(), READ_CONTROL | WRITE_DAC,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));

        if (!vol) {
                return GetLastError();
        }

        const SECURITY_INFORMATION sinfo = DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;
        auto ret = SetSecurityInfo(vol.get(), SE_KERNEL_OBJECT, sinfo, nullptr, nullptr, dacl, nullptr);

        if (ret == ERROR_SUCCESS && drive_letter) {
                wchar_t root[] { drive_letter, L':', L'\\', L'\0' };
                ret = SetNamedSecurityInfo(root, SE_FILE_OBJECT, sinfo, nullptr, nullptr, dacl, nullptr);
        }

        return ret;
}

/*
 * @param vol_guid_path is e.g. \\?\Volume{guid}\ or Volume{guid}
 */
std::expected<std::wstring, DWORD> resolve_volume_device(_In_ const std::wstring &vol_guid_path)
{
        auto name = vol_guid_path;

        if (std::wstring_view prefix(L"\\\\?\\"); name.starts_with(prefix)) {
                name = name.substr(prefix.size());
        }

        while (name.ends_with(L'\\')) {
                name.pop_back();
        }

        if (wchar_t target[MAX_PATH]; QueryDosDevice(name.c_str(), target, std::size(target))) {
                return target;
        }
        return std::unexpected(GetLastError());
}

std::expected<wchar_t, DWORD> find_volume_drive_letter(_In_ const std::filesystem::path &nt_device)
{
        if (nt_device.empty()) {
                return std::unexpected(ERROR_INVALID_PARAMETER);
        }

        wchar_t drive[] { L'\0', L':', L'\0' };

        for (wchar_t c = L'A'; c <= L'Z'; ++c) {
                *drive = c;
                if (wchar_t target[MAX_PATH]; !QueryDosDevice(drive, target, std::size(target))) {
                        return std::unexpected(GetLastError());
                } else if (std::filesystem::path(target) == nt_device) {
                        return c;
                }
        }

        return std::unexpected(ERROR_NOT_FOUND);
}

std::expected<std::vector<DWORD>, DWORD> get_disks_for_port(_In_ int port)
{
        hdevinfo di(SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK_LOCAL, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
        if (!di) {
                return std::unexpected(GetLastError());
        }

        std::vector<DWORD> disk_numbers;
        SP_DEVINFO_DATA dd{};
        dd.cbSize = sizeof(dd);

        for (DWORD i = 0; ; ++i) {
                if (!SetupDiEnumDeviceInfo(di.get(), i, &dd)) {
                        if (auto err = GetLastError(); err != ERROR_NO_MORE_ITEMS) {
                                return std::unexpected(err);
                        }
                        break;
                }

                DEVINST usbstor_inst{};
                if (CM_Get_Parent(&usbstor_inst, dd.DevInst, 0) != CR_SUCCESS) {
                        continue;
                }

                DEVINST usb_inst{};
                if (CM_Get_Parent(&usb_inst, usbstor_inst, 0) != CR_SUCCESS) {
                        continue;
                }

                DEVPROPTYPE prop_type{};
                ULONG port_num{};
                ULONG prop_sz = sizeof(port_num);
                if (CM_Get_DevNode_Property(usb_inst, &DEVPKEY_Device_Address, &prop_type,
                                            reinterpret_cast<PBYTE>(&port_num), &prop_sz, 0) != CR_SUCCESS) {
                        continue;
                }

                if (port_num != static_cast<ULONG>(port)) {
                        continue;
                }

                // Match found! Get the disk interface detail to open and query IOCTL_STORAGE_GET_DEVICE_NUMBER
                SP_DEVICE_INTERFACE_DATA idata{};
                idata.cbSize = sizeof(idata);
                if (SetupDiEnumDeviceInterfaces(di.get(), &dd, &GUID_DEVINTERFACE_DISK_LOCAL, 0, &idata)) {
                        DWORD req_sz{};
                        SetupDiGetDeviceInterfaceDetail(di.get(), &idata, nullptr, 0, &req_sz, nullptr);
                        if (req_sz) {
                                std::vector<BYTE> detail_buf(req_sz);
                                auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(detail_buf.data());
                                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
                                if (SetupDiGetDeviceInterfaceDetail(di.get(), &idata, detail, req_sz, nullptr, nullptr)) {
                                        Handle disk(CreateFile(detail->DevicePath, 0,
                                                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
                                        if (disk) {
                                                STORAGE_DEVICE_NUMBER sdn{};
                                                DWORD ret{};
                                                if (DeviceIoControl(disk.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                                                    nullptr, 0, &sdn, sizeof(sdn), &ret, nullptr)) {
                                                        disk_numbers.push_back(sdn.DeviceNumber);
                                                }
                                        }
                                }
                        }
                }
        }

        return disk_numbers;
}

struct volume_info
{
        std::wstring vol_guid_path;
        std::wstring nt_device;
        DWORD disk_number{};
};

std::expected<std::vector<volume_info>, DWORD> get_volumes_for_disks(_In_ const std::vector<DWORD> &disk_numbers)
{
        std::vector<volume_info> volumes;
        if (disk_numbers.empty()) {
                return volumes;
        }

        hdevinfo di(SetupDiGetClassDevs(&GUID_DEVINTERFACE_VOLUME_LOCAL, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
        if (!di) {
                return std::unexpected(GetLastError());
        }

        SP_DEVICE_INTERFACE_DATA idata{};
        idata.cbSize = sizeof(idata);

        for (DWORD i = 0; ; ++i) {
                if (!SetupDiEnumDeviceInterfaces(di.get(), nullptr, &GUID_DEVINTERFACE_VOLUME_LOCAL, i, &idata)) {
                        if (auto err = GetLastError(); err != ERROR_NO_MORE_ITEMS) {
                                return std::unexpected(err);
                        }
                        break;
                }

                DWORD req_sz{};
                SetupDiGetDeviceInterfaceDetail(di.get(), &idata, nullptr, 0, &req_sz, nullptr);
                if (!req_sz) {
                        continue;
                }

                std::vector<BYTE> detail_buf(req_sz);
                auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(detail_buf.data());
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                if (!SetupDiGetDeviceInterfaceDetail(di.get(), &idata, detail, req_sz, nullptr, nullptr)) {
                        continue;
                }

                Handle vol(CreateFile(detail->DevicePath, 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
                if (!vol) {
                        continue;
                }

                VOLUME_DISK_EXTENTS extents{};
                DWORD ret{};
                if (DeviceIoControl(vol.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                    nullptr, 0, &extents, sizeof(extents), &ret, nullptr)) {
                        if (extents.NumberOfDiskExtents > 0) {
                                auto dnum = extents.Extents[0].DiskNumber;
                                if (std::ranges::find(disk_numbers, dnum) != disk_numbers.end()) {
                                        if (auto nt_dev = resolve_volume_device(detail->DevicePath)) {
                                                volume_info vi {
                                                        .vol_guid_path = detail->DevicePath,
                                                        .nt_device = std::move(*nt_dev),
                                                        .disk_number = dnum,
                                                };
                                                volumes.push_back(std::move(vi));
                                        }
                                }
                        }
                }
        }

        return volumes;
}

} // namespace


std::vector<usbip::isolated_volume> usbip::isolate_port_volumes(int port, unsigned long timeout_ms)
{
        auto start = std::chrono::steady_clock::now();
        std::vector<DWORD> disks;

        while (true) {
                if (auto res = get_disks_for_port(port); res && !res->empty()) {
                        disks = std::move(*res);
                        break;
                }
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
                if (elapsed >= timeout_ms) {
                        break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (disks.empty()) {
                return {};
        }

        // Wait for volume(s) on the disk(s) to arrive
        std::vector<volume_info> vols;
        while (true) {
                if (auto res = get_volumes_for_disks(disks); res && !res->empty()) {
                        vols = std::move(*res);
                        break;
                }
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
                if (elapsed >= timeout_ms) {
                        break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::vector<isolated_volume> result;

        for (const auto &v: vols) {
                auto drive_letter = find_volume_drive_letter(v.nt_device);
                if (!drive_letter) {
                        wchar_t drive[] { *drive_letter, L':', L'\0' };

                        // 1. Remove the global mount point from \GLOBAL??
                        DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH,
                                        drive, v.nt_device.c_str());

                        // 2. Define the drive letter inside the caller's session
                        DefineDosDevice(DDD_RAW_TARGET_PATH, drive, v.nt_device.c_str());

                        libusbip::output("Isolated drive {}: for port {} to current session",
                                         static_cast<char>(*drive_letter), port);
                }

                // 3. Harden the volume DACL
                auto err = harden_volume_dacl(v.vol_guid_path, drive_letter ? *drive_letter : L'\0');
                if (err == ERROR_SUCCESS) {
                        libusbip::output("Hardened volume DACL for port {} ({})", port,
                                         drive_letter ? static_cast<char>(*drive_letter) : '?');
                }

                isolated_volume vol {
                        .volume_device = v.nt_device,
                        .drive_letter = drive_letter ? *drive_letter : L'?',
                        .dacl_hardened = err == ERROR_SUCCESS,
                };

                result.push_back(std::move(vol));
        }

        return result;
}

bool usbip::cleanup_port_volumes(int port)
{
        auto disks = get_disks_for_port(port);
        if (!disks) {
                SetLastError(disks.error());
                return false;
        }

        if (disks->empty()) {
                return false;
        }

        auto vols = get_volumes_for_disks(*disks);
        if (!vols) {
                SetLastError(vols.error());
                return false;
        }

        for (const auto &v: *vols) {
                if (auto letter = find_volume_drive_letter(v.nt_device)) {
                        wchar_t drive[] = { *letter, L':', L'\0' };
                        DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH,
                                        drive, v.nt_device.c_str());
                }
        }

        return true;
}
