/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>

#include <filesystem>
#include <libusbip\setupdi.h>

/*
* See: devcon utility
* https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
*/

namespace
{

enum { EXIT_USAGE = EXIT_FAILURE + 1 };

/*
 * @return REG_MULTI_SZ 
 */
auto make_hwid(LPCWSTR hwid)
{
        std::wstring s(hwid);
        s += L'\0'; // first string
        s += L'\0'; // end of the list
        return s;
}

void prompt_reboot()
{
        switch (auto ret = SetupPromptReboot(nullptr, nullptr, false)) {
        case SPFILEQ_REBOOT_IN_PROGRESS:
                wprintf(L"Rebooting...\n");
                break;
        case SPFILEQ_REBOOT_RECOMMENDED:
                wprintf(L"Reboot is recommended\n");
                break;
        default:
                assert(ret == -1);
                fwprintf(stderr, L"SetupPromptReboot error %#lx\n", GetLastError());
        }
}

/*
 * @param infpath must be an absolute path
 * @see devcon, cmd_install/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto install_devnode_and_driver(const std::wstring &infpath, LPCWSTR hwid)
{
        GUID ClassGUID;
        WCHAR ClassName[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClass(infpath.c_str(), &ClassGUID, ClassName, ARRAYSIZE(ClassName), 0)) {
                fwprintf(stderr, L"SetupDiGetINFClass('%s') error %#lx\n", infpath.c_str(), GetLastError());
                return EXIT_FAILURE;
        }

        auto dev_list = usbip::hdevinfo(SetupDiCreateDeviceInfoList(&ClassGUID, nullptr));
        if (!dev_list) {
                fwprintf(stderr, L"SetupDiCreateDeviceInfoList error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        SP_DEVINFO_DATA dev_data{ sizeof(dev_data) };
        if (!SetupDiCreateDeviceInfo(dev_list.get(), ClassName, &ClassGUID, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
                fwprintf(stderr, L"SetupDiCreateDeviceInfo error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        auto id = make_hwid(hwid);
        auto id_sz = DWORD(id.length()*sizeof(id[0]));

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                                              reinterpret_cast<const BYTE*>(id.data()), id_sz)) {
                fwprintf(stderr, L"SetupDiSetDeviceRegistryProperty error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, dev_list.get(), &dev_data)) {
                fwprintf(stderr, L"SetupDiCallClassInstaller error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        SP_DEVINSTALL_PARAMS params{ .cbSize = sizeof(params) };
        if (!SetupDiGetDeviceInstallParams(dev_list.get(), &dev_data, &params)) {
                fwprintf(stderr, L"SetupDiGetDeviceInstallParams error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }
        auto reboot_required = params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART);

        // the same as "pnputil /add-driver usbip2_vhci.inf /install"

        BOOL RebootRequired;
        if (!UpdateDriverForPlugAndPlayDevices(nullptr, hwid, infpath.c_str(), INSTALLFLAG_FORCE, &RebootRequired)) {
                fwprintf(stderr, L"UpdateDriverForPlugAndPlayDevices error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        if (reboot_required || RebootRequired) {
                prompt_reboot();
        }

        return EXIT_SUCCESS;
}

void usage(const std::wstring &program)
{
        auto &fmt = 
LR"(Usage: %s <command>
Commands:
install <inf> <hwid>    install a device node and its driver
)";

        wprintf(fmt, program.c_str());
}

} // namespace


int wmain(int argc, wchar_t* argv[]) // _tmain if include <tchar.h>
{
        if (argc < 2) {
                // 
        } else if (auto cmd = std::wstring_view(argv[1]); argc == 4 && cmd == L"install") {
                auto infpath = absolute(std::filesystem::path(argv[2]));
                auto hwid = argv[3];
                return install_devnode_and_driver(infpath.wstring(), hwid);
        }

        usage(std::filesystem::path(*argv).stem());
        return EXIT_USAGE;
}
