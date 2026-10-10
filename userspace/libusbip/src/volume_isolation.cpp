/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../volume_isolation.h"
#include "../format_message.h"
#include "output.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <sddl.h>
#include <aclapi.h>
#include <winioctl.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace
{

// GUID_DEVINTERFACE_DISK = {53f56307-b6bf-11d0-94f2-00a0c91efb8b}
DEFINE_GUID(GUID_DEVINTERFACE_DISK_LOCAL,
        0x53f56307, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

// GUID_DEVINTERFACE_VOLUME = {53f5630d-b6bf-11d0-94f2-00a0c91efb8b}
DEFINE_GUID(GUID_DEVINTERFACE_VOLUME_LOCAL,
        0x53f5630d, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

struct devinfo_cleanup
{
        HDEVINFO h;
        ~devinfo_cleanup() { if (h && h != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(h); }
};

struct handle_cleanup
{
        HANDLE h;
        ~handle_cleanup() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

struct local_free_cleanup
{
        HLOCAL p;
        ~local_free_cleanup() { if (p) LocalFree(p); }
};

std::wstring get_token_sddl_entries()
{
        HANDLE token_raw{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_raw)) {
                return {};
        }
        handle_cleanup token_guard{ token_raw };

        std::wstring sddl_entries;

        // Query User SID
        DWORD user_sz{};
        GetTokenInformation(token_raw, TokenUser, nullptr, 0, &user_sz);
        if (user_sz) {
                std::vector<BYTE> user_buf(user_sz);
                if (GetTokenInformation(token_raw, TokenUser, user_buf.data(), user_sz, &user_sz)) {
                        auto tu = reinterpret_cast<TOKEN_USER*>(user_buf.data());
                        PWSTR sid_str{};
                        if (ConvertSidToStringSidW(tu->User.Sid, &sid_str)) {
                                local_free_cleanup sid_guard{ sid_str };
                                sddl_entries += L"(A;;GA;;;";
                                sddl_entries += sid_str;
                                sddl_entries += L")";
                        }
                }
        }

        // Query Logon SID
        DWORD groups_sz{};
        GetTokenInformation(token_raw, TokenGroups, nullptr, 0, &groups_sz);
        if (groups_sz) {
                std::vector<BYTE> groups_buf(groups_sz);
                if (GetTokenInformation(token_raw, TokenGroups, groups_buf.data(), groups_sz, &groups_sz)) {
                        auto tg = reinterpret_cast<TOKEN_GROUPS*>(groups_buf.data());
                        for (DWORD i = 0; i < tg->GroupCount; ++i) {
                                if ((tg->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID) {
                                        PWSTR logon_sid_str{};
                                        if (ConvertSidToStringSidW(tg->Groups[i].Sid, &logon_sid_str)) {
                                                local_free_cleanup logon_guard{ logon_sid_str };
                                                sddl_entries += L"(A;;GA;;;";
                                                sddl_entries += logon_sid_str;
                                                sddl_entries += L")";
                                        }
                                        break;
                                }
                        }
                }
        }

        return sddl_entries;
}

bool harden_volume_dacl(_In_ const std::wstring &vol_path, _In_opt_ wchar_t drive_letter)
{
        auto caller_sddl = get_token_sddl_entries();
        if (caller_sddl.empty()) {
                return false;
        }

        // Protect DACL, allow only SYSTEM (SY) and current session / user.
        // Administrators (BA) is intentionally omitted to deny access to foreign admins.
        std::wstring sddl = L"D:P(A;;GA;;;SY)" + caller_sddl;

        PSECURITY_DESCRIPTOR pSD{};
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &pSD, nullptr)) {
                return false;
        }
        local_free_cleanup sd_guard{ pSD };

        PACL pDacl{};
        BOOL dacl_present{ FALSE };
        BOOL dacl_defaulted{ FALSE };
        GetSecurityDescriptorDacl(pSD, &dacl_present, &pDacl, &dacl_defaulted);

        HANDLE hVol = CreateFileW(vol_path.c_str(), READ_CONTROL | WRITE_DAC,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

        bool success = false;
        if (hVol != INVALID_HANDLE_VALUE) {
                handle_cleanup vol_guard{ hVol };
                if (SetSecurityInfo(hVol, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                    nullptr, nullptr, pDacl, nullptr) == ERROR_SUCCESS) {
                        success = true;
                }
        }

        if (drive_letter) {
                wchar_t root[4] = { drive_letter, L':', L'\\', L'\0' };
                SetNamedSecurityInfoW(root, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                      nullptr, nullptr, pDacl, nullptr);
        }

        return success;
}

std::wstring resolve_volume_device(_In_ const std::wstring &vol_guid_path)
{
        // vol_guid_path is e.g. \\?\Volume{guid}\ or Volume{guid}
        std::wstring name = vol_guid_path;
        if (name.starts_with(L"\\\\?\\")) {
                name = name.substr(4);
        }
        while (name.ends_with(L'\\')) {
                name.pop_back();
        }

        wchar_t target[MAX_PATH]{};
        if (QueryDosDeviceW(name.c_str(), target, MAX_PATH)) {
                return target;
        }
        return {};
}

wchar_t find_volume_drive_letter(_In_ const std::wstring &nt_device)
{
        if (nt_device.empty()) {
                return 0;
        }

        wchar_t drive[3] = { L'A', L':', L'\0' };
        for (wchar_t c = L'A'; c <= L'Z'; ++c) {
                drive[0] = c;
                wchar_t target[MAX_PATH]{};
                if (QueryDosDeviceW(drive, target, MAX_PATH)) {
                        if (_wcsicmp(target, nt_device.c_str()) == 0) {
                                return c;
                        }
                }
        }
        return 0;
}

std::vector<DWORD> get_disks_for_port(_In_ int port)
{
        std::vector<DWORD> disk_numbers;

        auto di = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK_LOCAL, nullptr, nullptr,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (di == INVALID_HANDLE_VALUE) {
                return disk_numbers;
        }
        devinfo_cleanup di_guard{ di };

        SP_DEVINFO_DATA dd{};
        dd.cbSize = sizeof(dd);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); ++i) {
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
                if (CM_Get_DevNode_PropertyW(usb_inst, &DEVPKEY_Device_Address, &prop_type,
                                             reinterpret_cast<PBYTE>(&port_num), &prop_sz, 0) != CR_SUCCESS) {
                        continue;
                }

                if (port_num != static_cast<ULONG>(port)) {
                        continue;
                }

                // Match found! Get the disk interface detail to open and query IOCTL_STORAGE_GET_DEVICE_NUMBER
                SP_DEVICE_INTERFACE_DATA idata{};
                idata.cbSize = sizeof(idata);
                if (SetupDiEnumDeviceInterfaces(di, &dd, &GUID_DEVINTERFACE_DISK_LOCAL, 0, &idata)) {
                        DWORD req_sz{};
                        SetupDiGetDeviceInterfaceDetailW(di, &idata, nullptr, 0, &req_sz, nullptr);
                        if (req_sz) {
                                std::vector<BYTE> detail_buf(req_sz);
                                auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detail_buf.data());
                                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
                                if (SetupDiGetDeviceInterfaceDetailW(di, &idata, detail, req_sz, nullptr, nullptr)) {
                                        HANDLE hDisk = CreateFileW(detail->DevicePath, 0,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                                        if (hDisk != INVALID_HANDLE_VALUE) {
                                                handle_cleanup disk_guard{ hDisk };
                                                STORAGE_DEVICE_NUMBER sdn{};
                                                DWORD ret{};
                                                if (DeviceIoControl(hDisk, IOCTL_STORAGE_GET_DEVICE_NUMBER,
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

std::vector<volume_info> get_volumes_for_disks(_In_ const std::vector<DWORD> &disk_numbers)
{
        std::vector<volume_info> volumes;
        if (disk_numbers.empty()) {
                return volumes;
        }

        auto di = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_VOLUME_LOCAL, nullptr, nullptr,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (di == INVALID_HANDLE_VALUE) {
                return volumes;
        }
        devinfo_cleanup di_guard{ di };

        SP_DEVICE_INTERFACE_DATA idata{};
        idata.cbSize = sizeof(idata);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &GUID_DEVINTERFACE_VOLUME_LOCAL, i, &idata); ++i) {
                DWORD req_sz{};
                SetupDiGetDeviceInterfaceDetailW(di, &idata, nullptr, 0, &req_sz, nullptr);
                if (!req_sz) {
                        continue;
                }

                std::vector<BYTE> detail_buf(req_sz);
                auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detail_buf.data());
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                if (!SetupDiGetDeviceInterfaceDetailW(di, &idata, detail, req_sz, nullptr, nullptr)) {
                        continue;
                }

                HANDLE hVol = CreateFileW(detail->DevicePath, 0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (hVol == INVALID_HANDLE_VALUE) {
                        continue;
                }
                handle_cleanup vol_guard{ hVol };

                VOLUME_DISK_EXTENTS extents{};
                DWORD ret{};
                if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                    nullptr, 0, &extents, sizeof(extents), &ret, nullptr)) {
                        if (extents.NumberOfDiskExtents > 0) {
                                auto dnum = extents.Extents[0].DiskNumber;
                                if (std::ranges::find(disk_numbers, dnum) != disk_numbers.end()) {
                                        auto nt_dev = resolve_volume_device(detail->DevicePath);
                                        volumes.push_back(volume_info{
                                                .vol_guid_path = detail->DevicePath,
                                                .nt_device = std::move(nt_dev),
                                                .disk_number = dnum,
                                        });
                                }
                        }
                }
        }

        return volumes;
}

} // namespace

std::vector<usbip::isolated_volume> usbip::isolate_port_volumes(_In_ int port, _In_ DWORD timeout_ms)
{
        auto start = std::chrono::steady_clock::now();
        std::vector<DWORD> disks;

        while (true) {
                disks = get_disks_for_port(port);
                if (!disks.empty()) {
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
                vols = get_volumes_for_disks(disks);
                if (!vols.empty()) {
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
                if (drive_letter) {
                        wchar_t drive[3] = { drive_letter, L':', L'\0' };

                        // 1. Remove the global mount point from \GLOBAL??
                        DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH,
                                         drive, v.nt_device.c_str());

                        // 2. Define the drive letter inside the caller's session
                        DefineDosDeviceW(DDD_RAW_TARGET_PATH, drive, v.nt_device.c_str());

                        libusbip::output("Isolated drive {}: for port {} to current session",
                                         static_cast<char>(drive_letter), port);
                }

                // 3. Harden the volume DACL
                auto hardened = harden_volume_dacl(v.vol_guid_path, drive_letter);
                if (hardened) {
                        libusbip::output("Hardened volume DACL for port {} ({})", port,
                                         drive_letter ? static_cast<char>(drive_letter) : '?');
                }

                result.push_back(isolated_volume{
                        .volume_device = v.nt_device,
                        .drive_letter = drive_letter,
                        .dacl_hardened = hardened,
                });
        }

        return result;
}

bool usbip::cleanup_port_volumes(_In_ int port)
{
        auto disks = get_disks_for_port(port);
        if (disks.empty()) {
                return false;
        }

        auto vols = get_volumes_for_disks(disks);
        for (const auto &v: vols) {
                auto drive_letter = find_volume_drive_letter(v.nt_device);
                if (drive_letter) {
                        wchar_t drive[3] = { drive_letter, L':', L'\0' };
                        DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH,
                                         drive, v.nt_device.c_str());
                }
        }

        return true;
}
