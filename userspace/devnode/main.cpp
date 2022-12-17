/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <regstr.h>

#include <cassert>
#include <string>
#include <vector>
#include <filesystem>

#include <libusbip\hkey.h>
#include <libusbip\setupdi.h>

/*
* See: devcon utility
* https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
*/

namespace
{

enum { EXIT_USAGE = EXIT_FAILURE + 1 };

auto split_multi_sz(PCWSTR str, const std::wstring_view &exclude, bool &excluded)
{
        std::vector<std::wstring> v;

        while (str && *str) {
                std::wstring_view s(str);
                if (s == exclude) {
                        excluded = true;
                } else {
                        v.emplace_back(s);
                }
                str += s.size() + 1; // skip L'\0'
        }

        return v;
}

auto make_multi_sz(const std::vector<std::wstring> &v)
{
        std::wstring str;

        for (auto &s: v) {
                str += s;
                str += L'\0';
        }

        str += L'\0';
        return str;
}

/*
 * @return REG_MULTI_SZ 
 */
auto make_multi_sz_hwid(LPCWSTR hwid)
{
        std::wstring s(hwid);
        s += L'\0'; // first string
        s += L'\0'; // end of the list
        return s;
}

auto read_multi_z(HKEY key, LPCWSTR val_name, std::vector<WCHAR> &val)
{
        for (auto val_sz = DWORD(val.size()); ; ) {
                switch (auto err = RegGetValue(key, nullptr, val_name, RRF_RT_REG_MULTI_SZ, nullptr, 
                                               reinterpret_cast<BYTE*>(val.data()), &val_sz)) {
                case ERROR_FILE_NOT_FOUND: // val_name
                        val.clear();
                        [[fallthrough]];
                case ERROR_SUCCESS:
                        return true;
                case ERROR_MORE_DATA:
                        val.resize(val_sz);
                        break;
                default:
                        fprintf(stderr, "RegGetValue('%S') error %ld\n", val_name, err);
                        return false;
                }
        }
}

void prompt_reboot()
{
        switch (auto ret = SetupPromptReboot(nullptr, nullptr, false)) {
        case SPFILEQ_REBOOT_IN_PROGRESS:
                printf("Rebooting...\n");
                break;
        case SPFILEQ_REBOOT_RECOMMENDED:
                printf("Reboot is recommended\n");
                break;
        default:
                assert(ret == -1);
                fprintf(stderr, "SetupPromptReboot error %#lx\n", GetLastError());
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
                fprintf(stderr, "SetupDiGetINFClass('%S') error %#lx\n", ClassName, GetLastError());
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

        auto hwid_mz = make_multi_sz_hwid(hwid);

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                reinterpret_cast<const BYTE*>(hwid_mz.data()), DWORD(hwid_mz.length()*sizeof(*hwid_mz.data())))) {
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

        if (reboot_required || RebootRequired) {
                prompt_reboot();
        }

        return EXIT_SUCCESS;
}

/*
* @see devcon, cmdClassFilter
*/
auto filter(const std::wstring_view &guid, const std::wstring_view &driver, bool upper, bool add)
{
        GUID ClassGUID;
        if (auto err = CLSIDFromString(guid.data(), &ClassGUID)) {
                fprintf(stderr, "CLSIDFromString('%S') error %#lx\n", guid.data(), err);
                return EXIT_FAILURE;
        }

        usbip::HKey key(SetupDiOpenClassRegKeyEx(&ClassGUID, KEY_QUERY_VALUE | KEY_SET_VALUE, DIOCR_INSTALLER, nullptr, nullptr));
        if (!key) {
                fprintf(stderr, "SetupDiOpenClassRegKeyEx('%S') error %#lx\n", guid.data(), GetLastError());
                return EXIT_FAILURE;
        }

        auto val_name = upper ? REGSTR_VAL_UPPERFILTERS : REGSTR_VAL_LOWERFILTERS;
        std::vector<WCHAR> val(4096);

        if (!read_multi_z(key.get(), val_name, val)) {
                return EXIT_FAILURE;
        }

        auto modified = add;
        auto filters = split_multi_sz(val.empty() ? nullptr : val.data(), driver, modified);
        if (add) {
                filters.emplace_back(driver);
        }

        if (!modified) {
                return EXIT_SUCCESS;

        }

        if (auto str = make_multi_sz(filters); 
            auto err = RegSetValueEx(key.get(), val_name, 0, REG_MULTI_SZ,
                                     reinterpret_cast<const BYTE*>(str.data()), DWORD(str.length()*sizeof(str[0])))) {
                fprintf(stderr, "RegSetValueEx('%S') error %ld\n", val_name, err);
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

void usage(const wchar_t *argv0)
{
        const char fmt[] = 
"Usage: %S <command> <arg>...\n"
"Commands:\n"
"install <inf> <hwid>\tinstall a device node and its driver\n"
"filter <add|remove> <upper|lower> <ClassGuid> <driver>\tadd or remove class filter driver\n";

        auto program = std::filesystem::path(argv0).stem();
        printf(fmt, program.c_str());
}

auto parse_add_remove(const std::wstring_view &s)
{
        if (s == L"add") {
                return 1;
        }
      
        if (s == L"remove") {
                return 0;
        }

        return -1;
}

auto parse_upper_lower(const std::wstring_view &s)
{
        if (s == L"upper") {
                return 1;
        }

        if (s == L"lower") {
                return 0;
        }

        return -1;
}

} // namespace


int wmain(int argc, wchar_t* argv[]) // _tmain if include <tchar.h>
{
        if (argc < 2) {
                // 
        } else if (auto cmd = std::wstring_view(argv[1]); argc == 4 && cmd == L"install") {
                auto infpath = absolute(std::filesystem::path(argv[2]));
                return install_devnode_and_driver(infpath.wstring(), argv[3]);
        } else if (argc == 6 && cmd == L"filter") {
                auto add = parse_add_remove(argv[2]);
                auto upper = parse_upper_lower(argv[3]);
                if (add >= 0 && upper >= 0) {
                        auto guid = argv[4];
                        auto driver = argv[5];
                        return filter(guid, driver, upper, add);
                }
        }

        usage(*argv);
        return EXIT_USAGE;
}
