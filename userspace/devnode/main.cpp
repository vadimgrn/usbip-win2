/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>

#include <string>
#include <filesystem>

#include <libusbip\setupdi.h>

/*
* See: devcon utility, cmd_install/cmd_remove.
* https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
*/

namespace
{

enum { EXIT_USAGE = EXIT_FAILURE + 1 };

/*
 * @return REG_MULTI_SZ 
 */
auto make_hwid_multiz(LPCWSTR hwid)
{
        std::wstring s(hwid);
        s += L'\0'; // first string
        s += L'\0'; // end of the list
        return s;
}

/*
 * @param infpath must be an absolute path
 * @see devcon hwids "USBIP\*"
 */
auto install_devnode_and_driver(const std::wstring &infpath, LPCWSTR hwid)
{
        GUID ClassGUID;
        WCHAR ClassName[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClass(infpath.c_str(), &ClassGUID, ClassName, ARRAYSIZE(ClassName), 0)) {
                fprintf(stderr, "SetupDiGetINFClass error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        auto dev_list = usbip::hdevinfo(SetupDiCreateDeviceInfoList(&ClassGUID, nullptr));
        if (!dev_list) {
                fprintf(stderr, "SetupDiCreateDeviceInfoList error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        SP_DEVINFO_DATA dev_data{ sizeof(dev_data) };
        if (!SetupDiCreateDeviceInfo(dev_list.get(), ClassName, &ClassGUID, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
                fprintf(stderr, "SetupDiCreateDeviceInfo error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        auto hwid_multiz = make_hwid_multiz(hwid);

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                reinterpret_cast<const BYTE*>(hwid_multiz.data()), DWORD(hwid_multiz.length()*sizeof(*hwid_multiz.data())))) {
                fprintf(stderr, "SetupDiSetDeviceRegistryProperty error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, dev_list.get(), &dev_data)) {
                fprintf(stderr, "SetupDiCallClassInstaller error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        SP_DEVINSTALL_PARAMS params{ .cbSize = sizeof(params) };
        if (!SetupDiGetDeviceInstallParams(dev_list.get(), &dev_data, &params)) {
                fprintf(stderr, "SetupDiGetDeviceInstallParams error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }
        auto reboot_required = params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART);

        // the same as "pnputil /add-driver usbip2_vhci.inf /install"

        BOOL RebootRequired;
        if (!UpdateDriverForPlugAndPlayDevices(nullptr, hwid, infpath.c_str(), INSTALLFLAG_FORCE, &RebootRequired)) {
                fprintf(stderr, "UpdateDriverForPlugAndPlayDevices error %#lx\n", GetLastError());
                return EXIT_FAILURE;
        }

        if ((reboot_required || RebootRequired) && SetupPromptReboot(nullptr, nullptr, false) < 0 ) {
                fprintf(stderr, "SetupPromptReboot error %#lx\n", GetLastError());
        }

        return EXIT_SUCCESS;
}

void usage(const std::wstring &program)
{
        const char fmt[] = 
"Usage: %S <command> <arg>...\n"
"Commands:\n"
"install <inf> <hwid>\tinstall a device node and its driver\n";                

        printf(fmt, program.c_str());
}

} // namespace


int wmain(int argc, wchar_t* argv[]) // _tmain if include <tchar.h>
{
        if (argc >= 2) {
                auto cmd = std::wstring_view(argv[1]);
                if (argc == 4 && cmd == L"install") {
                        auto infpath = absolute(std::filesystem::path(argv[2]));
                        return install_devnode_and_driver(infpath.wstring(), argv[3]);
                }
        }

        usage(std::filesystem::path(*argv).stem());
        return EXIT_USAGE;
}
