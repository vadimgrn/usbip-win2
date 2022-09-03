/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <strsafe.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <devguid.h>

#include <cstdio>
#include <cassert>
#include <memory>
#include <string>

#include <initguid.h>
#include <Devpkey.h>
#include "usbip_vhci_api.h"

#include "usbip_setupdi.h"

/*
* See: devcon utility, cmd_install/cmd_remove.
* https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
*/

namespace
{

enum { EXIT_USAGE = EXIT_FAILURE + 1 };

auto DiCreateDeviceInfoList(const GUID &ClassGUID) noexcept
{
        usbip::hdevinfo h(SetupDiCreateDeviceInfoList(&ClassGUID, nullptr));
        if (!h) {
                fprintf(stderr, "SetupDiCreateDeviceInfoList error %#lx\n", GetLastError());
        }
        return h;
}

auto get_full_path(WCHAR *path, DWORD cch, LPCWSTR inf) noexcept
{
        auto cnt = GetFullPathName(inf, cch, path, nullptr);

        if (!cnt) {
                fprintf(stderr, "GetFullPathName('%S') error %#lx\n", inf, GetLastError());
        } else if (cnt >= cch) {
                fprintf(stderr, "GetFullPathName('%S'): buffer is small\n", inf);
        } else if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) {
                fprintf(stderr, "GetFileAttributes error %#lx\n", GetLastError());
        } else {
                return EXIT_SUCCESS;
        }

        return EXIT_FAILURE;
}

inline void print_reboot_msg()
{
        printf("System reboot is required\n");
}

/*
 * cmd_install/cmd_update
 */
auto install_devnode_and_driver(LPCWSTR inf, LPCWSTR hwid) noexcept
{
        WCHAR infpath[MAX_PATH];
        if (auto err = get_full_path(infpath, ARRAYSIZE(infpath), inf)) {
                return err;
        }

        WCHAR hwid_list[LINE_LEN + 2]{};
        if (FAILED(StringCchCopy(hwid_list, LINE_LEN + 1, hwid))) {
                fprintf(stderr, "StringCchCopy error\n");
                return EXIT_FAILURE;
        }

        GUID ClassGUID;
        WCHAR ClassName[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClass(infpath, &ClassGUID, ClassName, ARRAYSIZE(ClassName), 0)) {
                fprintf(stderr, "SetupDiGetINFClass error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        auto dev_list = DiCreateDeviceInfoList(ClassGUID);
        if (!dev_list) {
                return EXIT_FAILURE;
        }

        SP_DEVINFO_DATA dev_data{ sizeof(dev_data) };
        if (!SetupDiCreateDeviceInfo(dev_list.get(), ClassName, &ClassGUID, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
                fprintf(stderr, "SetupDiCreateDeviceInfo error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                (LPCBYTE)hwid_list, ((DWORD)wcslen(hwid_list) + 2)*sizeof(*hwid_list))) {
                fprintf(stderr, "SetupDiSetDeviceRegistryProperty error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, dev_list.get(), &dev_data)) {
                fprintf(stderr, "SetupDiCallClassInstaller error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        BOOL reboot{};

        if (!UpdateDriverForPlugAndPlayDevices(nullptr, hwid, infpath, INSTALLFLAG_FORCE, &reboot)) {
                fprintf(stderr, "UpdateDriverForPlugAndPlayDevices error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        } else if (reboot) {
                print_reboot_msg();
        }

        return EXIT_SUCCESS;
}

auto remove_devnode(_In_ LPCWSTR DeviceInstanceId) noexcept
{
        auto dev_list = DiCreateDeviceInfoList(GUID_DEVCLASS_SYSTEM);
        if (!dev_list) {
                return EXIT_FAILURE;
        }

        SP_DEVINFO_DATA	dev_data{ sizeof(dev_data) };

        if (!SetupDiOpenDeviceInfo(dev_list.get(), DeviceInstanceId, nullptr, 0, &dev_data)) {
                auto err = GetLastError();
                fprintf(stderr, "SetupDiOpenDeviceInfo(DeviceInstanceId='%S') error %#lx\n", DeviceInstanceId, err);
                return EXIT_FAILURE;
        }

        BOOL reboot{};

        if (!DiUninstallDevice(nullptr, dev_list.get(), &dev_data, 0, &reboot)) {
                fprintf(stderr, "DiUninstallDevice error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        } else if (reboot) {
                print_reboot_msg();
        }

        return EXIT_SUCCESS;
}

auto get_parent_device(_In_ HDEVINFO devinfo, _In_ SP_DEVINFO_DATA *devinfo_data)
{
        std::wstring parent;

        DEVPROPTYPE PropertyType;
        DWORD RequiredSize;

        SetupDiGetDeviceProperty(devinfo, devinfo_data, &DEVPKEY_Device_Parent, &PropertyType, nullptr, 0, &RequiredSize, 0);
        auto err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER) {
                fprintf(stderr, "SetupDiGetDeviceProperty(RequiredSize) error %#lx\n", err);
                return parent;
        }

        assert(PropertyType == DEVPROP_TYPE_STRING);
        auto buf = std::make_unique_for_overwrite<BYTE[]>(RequiredSize);

        if (!SetupDiGetDeviceProperty(devinfo, devinfo_data, &DEVPKEY_Device_Parent, &PropertyType, buf.get(), RequiredSize, nullptr, 0)) {
                fprintf(stderr, "SetupDiGetDeviceProperty error %#lx\n", GetLastError());
                return parent;
        }

        parent.assign(reinterpret_cast<wchar_t*>(buf.get()), RequiredSize/sizeof(parent[0]));
        return parent;
}

auto get_parent_device(_In_ HDEVINFO devinfo)
{
        std::wstring parent;
        SP_DEVINFO_DATA	devinfo_data{ sizeof(devinfo_data) };

        if (SetupDiEnumDeviceInfo(devinfo, 0, &devinfo_data)) {
                parent = get_parent_device(devinfo, &devinfo_data);
        } else {
                fprintf(stderr, "SetupDiEnumDeviceInfo error %#lx\n", GetLastError());
        }

        return parent;
}

auto get_root_instance_id(_In_ hci_version version)
{
        std::wstring inst_id;
        auto &guid = vhci_guid(version);

        if (auto h = usbip::GetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)) {
                inst_id = get_parent_device(h.get()); // VHCI -> ROOT
        } else {
                printf("SetupDiGetClassDevs error %#lx\n", GetLastError());
        }

        return inst_id;
}

/*
 * @see devcon hwids "USBIPWIN\*"
 */
auto uninstall()
{
        for (auto version: vhci_list) {
                auto inst_id = get_root_instance_id(version);
                if (!inst_id.empty()) {
                        printf("Removing %S\n", inst_id.c_str());
                        remove_devnode(inst_id.c_str());
                }
        }

        return EXIT_SUCCESS;
}

auto get_file_name(WCHAR *buf, DWORD cnt, const wchar_t *path) noexcept
{
        LPWSTR fname{};

        [[maybe_unused]] auto n = GetFullPathName(path, cnt, buf, &fname);
        assert(n > 0 && n < cnt);

        return fname;
}

void usage(const wchar_t *program) noexcept
{
        WCHAR path[MAX_PATH];
        auto fname = get_file_name(path, ARRAYSIZE(path), program);

        const char fmt[] = 
"Usage: %S <command> <arg>...\n"
"Commands:\n"
"install <inf> <hwid>\tinstall a device/node and its driver\n"
"uninstall\t\tuninstall usbip devices and remove its device nodes\n";
                
        printf(fmt, fname);
}

} // namespace


int wmain(int argc, wchar_t* argv[])
{
        if (argc >= 2) {
                auto cmd = argv[1];
                if (argc == 4 && !wcscmp(cmd, L"install")) {
                        return install_devnode_and_driver(argv[2], argv[3]);
                } else if (argc == 2 && !wcscmp(cmd, L"uninstall")) {
                        return uninstall();
                }
        }

        usage(*argv);
        return EXIT_USAGE;
}
