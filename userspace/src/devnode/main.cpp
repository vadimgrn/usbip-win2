#include <windows.h>
#include <strsafe.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
//#include <devguid.h>

#include <memory>
#include <cstdio>
#include <cassert>

/*
* See: devcon utility, cmd_install/cmd_remove.
* https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
*/

namespace
{

enum { EXIT_USAGE = EXIT_FAILURE + 1 };

/*
        if (*ClassGUID != GUID_DEVCLASS_SYSTEM) { // usbip_root.inf, ClassGuid
                fprintf(stderr, "GUID_DEVCLASS_SYSTEM expected\n");
                return ptr;
        }
*/
auto DiCreateDeviceInfoList(const GUID *ClassGUID) noexcept
{
        std::unique_ptr<void, decltype(SetupDiDestroyDeviceInfoList)&> ptr(nullptr, SetupDiDestroyDeviceInfoList);
        auto handle = SetupDiCreateDeviceInfoList(ClassGUID, nullptr);

        if (handle != INVALID_HANDLE_VALUE) {
                ptr.reset(handle);
        } else {
                fprintf(stderr, "SetupDiCreateDeviceInfoList error %#04x\n", GetLastError());
        }

        return ptr;
}

auto get_full_path(WCHAR *path, DWORD cch, LPCWSTR inf) noexcept
{
        auto cnt = GetFullPathName(inf, cch, path, nullptr);

        if (!cnt) {
                fprintf(stderr, "GetFullPathName('%S') error %#04x\n", inf, GetLastError());
        } else if (cnt >= cch) {
                fprintf(stderr, "GetFullPathName('%S'): buffer is small\n", inf);
        } else if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) {
                fprintf(stderr, "GetFileAttributes error %#04x\n", GetLastError());
        } else {
                return EXIT_SUCCESS;
        }

        return EXIT_FAILURE;
}

inline void print_reboot_msg()
{
        printf("reboot is required\n");
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
                fprintf(stderr, "SetupDiGetINFClass error %#04x\n", GetLastError());
                return EXIT_FAILURE;
        }

        auto dev_list = DiCreateDeviceInfoList(&ClassGUID);
        if (!dev_list) {
                return EXIT_FAILURE;
        }

        SP_DEVINFO_DATA dev_data{ sizeof(dev_data) };
        if (!SetupDiCreateDeviceInfo(dev_list.get(), ClassName, &ClassGUID, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
                fprintf(stderr, "SetupDiCreateDeviceInfo error %#04x\n", GetLastError());
                return EXIT_FAILURE;
        }

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                (LPCBYTE)hwid_list, ((DWORD)wcslen(hwid_list) + 2)*sizeof(*hwid_list))) {
                fprintf(stderr, "SetupDiSetDeviceRegistryProperty error %#04x\n", GetLastError());
                return EXIT_FAILURE;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, dev_list.get(), &dev_data)) {
                fprintf(stderr, "SetupDiCallClassInstaller error %#04x\n", GetLastError());
                return EXIT_FAILURE;
        }

        BOOL reboot{};

        if (!UpdateDriverForPlugAndPlayDevices(nullptr, hwid, infpath, INSTALLFLAG_FORCE, &reboot)) {
                fprintf(stderr, "UpdateDriverForPlugAndPlayDevices error %#04x\n", GetLastError());
                return EXIT_FAILURE;
        } else if (reboot) {
                print_reboot_msg();
        }

        return EXIT_SUCCESS;
}

/*
 * @see devcon hwids "USBIPWIN\*"
 */
auto remove_devnode(LPCWSTR DeviceInstanceId) noexcept
{
        auto dev_list = DiCreateDeviceInfoList(nullptr); // &GUID_DEVCLASS_SYSTEM
        if (!dev_list) {
                return EXIT_FAILURE;
        }

        SP_DEVINFO_DATA	dev_data{ sizeof(dev_data) };
        if (!SetupDiOpenDeviceInfo(dev_list.get(), DeviceInstanceId, nullptr, 0, &dev_data)) {
                fprintf(stderr, "SetupDiOpenDeviceInfo(DeviceInstanceId='%S') error %#04x\n", DeviceInstanceId, GetLastError());
                return EXIT_FAILURE;
        }

        BOOL reboot{};

        if (!DiUninstallDevice(nullptr, dev_list.get(), &dev_data, 0, &reboot)) {
                fprintf(stderr, "DiUninstallDevice error %#04x\n", GetLastError());
                return EXIT_FAILURE;
        } else if (reboot) {
                print_reboot_msg();
        }

        return EXIT_SUCCESS;
}

auto get_file_name(WCHAR *buf, DWORD cnt, const wchar_t *path) noexcept
{
        LPWSTR fname{};

        auto n = GetFullPathName(path, cnt, buf, &fname);
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
"remove <instanceid>\tuninstall a device and remove its device node\n";
                
        printf(fmt, fname);
}

} // namespace


int wmain(int argc, wchar_t* argv[])
{
        if (argc >= 2) {
                auto cmd = argv[1];
                if (argc == 4 && !wcscmp(cmd, L"install")) {
                        return install_devnode_and_driver(argv[2], argv[3]);
                } else if (argc == 3 && !wcscmp(cmd, L"remove")) {
                        return remove_devnode(argv[2]);
                }
        }

        usage(*argv);
        return EXIT_USAGE;
}
