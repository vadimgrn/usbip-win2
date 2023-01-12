/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <regstr.h>

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

auto split_multi_sz(_In_ PCWSTR str, _In_ const std::wstring_view &exclude, _Inout_ bool &excluded)
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

auto make_multi_sz(_In_ const std::vector<std::wstring> &v)
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
auto make_hwid(_In_ LPCWSTR hwid)
{
        std::wstring s(hwid);
        s += L'\0'; // first string
        s += L'\0'; // end of the list
        return s;
}

auto read_multi_z(_In_ HKEY key, _In_ LPCWSTR val_name, _Out_ std::vector<WCHAR> &val)
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
                        fwprintf(stderr, L"RegGetValue('%s') error %ld\n", val_name, err);
                        return false;
                }
        }
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
auto install_devnode_and_driver(_In_ const std::wstring &infpath, _In_ LPCWSTR hwid)
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

/*
 * devcon classfilter usb upper ; query
 * devcon classfilter usb upper !usbip2_filter ; remove
 * @see devcon, cmdClassFilter
 */
auto classfilter(
        _In_ const std::wstring_view &guid, 
        _In_ const std::wstring_view& driver,
        _In_ bool upper, _In_ bool add)
{
        GUID ClassGUID;
        if (auto err = CLSIDFromString(guid.data(), &ClassGUID)) {
                fwprintf(stderr, L"CLSIDFromString('%s') error %#lx\n", guid.data(), err);
                return EXIT_FAILURE;
        }

        usbip::HKey key(SetupDiOpenClassRegKeyEx(&ClassGUID, KEY_QUERY_VALUE | KEY_SET_VALUE, DIOCR_INSTALLER, nullptr, nullptr));
        if (!key) {
                fwprintf(stderr, L"SetupDiOpenClassRegKeyEx('%s') error %#lx\n", guid.data(), GetLastError());
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
                                     reinterpret_cast<const BYTE*>(str.data()), 
                                     DWORD(str.length()*sizeof(str[0])))) {
                fwprintf(stderr, L"RegSetValueEx('%s') error %ld\n", val_name, err);
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

auto parse_add_remove(_In_ const std::wstring_view &s)
{
        if (s == L"add") {
                return 1;
        }

        if (s == L"remove") {
                return 0;
        }

        return -1;
}

auto parse_upper_lower(_In_ const std::wstring_view &s)
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


int wmain(_In_ int argc, _Inout_ wchar_t* argv[]) // _tmain if include <tchar.h>
{
        auto command = std::filesystem::path(*argv).stem();

        if (command == L"devnode") {
                if (argc == 4 && std::wstring_view(argv[1]) == L"install") {
                        auto infpath = absolute(std::filesystem::path(argv[2]));
                        auto hwid = argv[3];
                        return install_devnode_and_driver(infpath.wstring(), hwid);
                }

                wprintf(L"%s install <inf> <hwid>\t"
                        L"install a device node and its driver\n", command.c_str());

        } else if (command == L"classfilter") {
                if (argc == 5) {
                        auto add = parse_add_remove(argv[1]);
                        auto upper = parse_upper_lower(argv[2]);
                        if (add >= 0 && upper >= 0) {
                                auto guid = argv[3];
                                auto driver = argv[4];
                                return classfilter(guid, driver, upper, add);
                        }
                }

                wprintf(L"%s <add|remove> <upper|lower> <ClassGuid> <driver>\t"
                        L"add or remove class filter driver\n", command.c_str());
        } else {
                wprintf(L"Unexpected command '%s'\n", command.c_str());
        }

        return EXIT_USAGE;
}
