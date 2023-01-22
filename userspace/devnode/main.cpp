/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <regstr.h>

#include <libusbip\hkey.h>
#include <libusbip\setupdi.h>
#include <libusbip\file_ver.h>
#include <libusbip\CLI11.hpp>

/*
 * See: devcon utility
 * https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
 */

namespace
{

struct devnode_args_t
{
        std::wstring infpath;
        std::wstring hwid;
};

struct classfilter_args_t
{
        bool add;
        std::string_view level;
        std::wstring class_guid;
        std::wstring driver_name;
};

auto &opt_upper = "upper";

auto get_version(_In_ const wchar_t *program)
{
        FileVersion fv(program);
        auto ver = fv.GetFileVersion();
        return CLI::narrow(ver);
}

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
auto make_hwid(_In_ std::wstring hwid)
{
        hwid += L'\0'; // first string
        hwid += L'\0'; // end of the list
        return hwid;
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
 * @see devcon, cmd/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto install_devnode_and_driver(_In_ const devnode_args_t &args)
{
        GUID ClassGUID;
        WCHAR ClassName[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClass(args.infpath.c_str(), &ClassGUID, ClassName, ARRAYSIZE(ClassName), 0)) {
                fwprintf(stderr, L"SetupDiGetINFClass('%s') error %#lx\n", args.infpath.c_str(), GetLastError());
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

        auto id = make_hwid(args.hwid);
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
        if (!UpdateDriverForPlugAndPlayDevices(nullptr, args.hwid.c_str(), args.infpath.c_str(), INSTALLFLAG_FORCE, &RebootRequired)) {
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
auto classfilter(_In_ const classfilter_args_t &args) 
{
        GUID ClassGUID;
        if (auto err = CLSIDFromString(args.class_guid.data(), &ClassGUID)) {
                fwprintf(stderr, L"CLSIDFromString('%s') error %#lx\n", args.class_guid.data(), err);
                return EXIT_FAILURE;
        }

        usbip::HKey key(SetupDiOpenClassRegKeyEx(&ClassGUID, KEY_QUERY_VALUE | KEY_SET_VALUE, DIOCR_INSTALLER, nullptr, nullptr));
        if (!key) {
                fwprintf(stderr, L"SetupDiOpenClassRegKeyEx('%s') error %#lx\n", args.class_guid.data(), GetLastError());
                return EXIT_FAILURE;
        }

        auto val_name = args.level == opt_upper ? REGSTR_VAL_UPPERFILTERS : REGSTR_VAL_LOWERFILTERS;
        std::vector<WCHAR> val(4096);
        if (!read_multi_z(key.get(), val_name, val)) {
                return EXIT_FAILURE;
        }

        auto modified = args.add;
        auto filters = split_multi_sz(val.empty() ? nullptr : val.data(), args.driver_name, modified);
        if (args.add) {
                filters.emplace_back(args.driver_name);
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

void add_devnode_cmds(_In_ CLI::App &app, _In_ devnode_args_t &args)
{
        auto cmd = app.add_subcommand("install", "Install a device node and its driver");

        cmd->add_option("infpath", args.infpath, "Path fo .inf file")
                ->check(CLI::ExistingFile)
                ->required();

        cmd->add_option("hwid", args.hwid, "Hardware Id of the device")->required();

        auto f = [&args] 
        { 
                if (auto err = install_devnode_and_driver(args)) {
                        exit(err);
                }
        };
        cmd->callback(std::move(f));
}

void add_classfilter_cmds(_In_ CLI::App &app, _In_ classfilter_args_t &args)
{
        auto &cmd_add = "add";

        for (auto action: {cmd_add, "remove"}) {

                auto cmd = app.add_subcommand(action, std::string(action) + " class filter driver");

                cmd->add_option("Level", args.level)
                        ->check(CLI::IsMember({opt_upper, "lower"}))
                        ->required();

                cmd->add_option("ClassGuid", args.class_guid)->required();
                cmd->add_option("DriverName", args.driver_name, "Filter driver name")->required();

                auto f = [&args, add = action == cmd_add]
                { 
                        args.add = add;
                        if (auto err = classfilter(args)) {
                                exit(err);
                        }
                };
                cmd->callback(std::move(f));
        }
}

} // namespace


int wmain(_In_ int argc, _Inout_ wchar_t* argv[])
{
        CLI::App app("usbip2 driver installation utility");
        app.set_version_flag("-V,--version", get_version(*argv));

        auto &devnode = L"devnode";
        devnode_args_t devnode_args;

        auto &classfilter = L"classfilter";
        classfilter_args_t classfilter_args;

        if (auto prog = std::filesystem::path(*argv).stem(); prog == devnode) {
                add_devnode_cmds(app, devnode_args);
        } else if (prog == classfilter) {
                add_classfilter_cmds(app, classfilter_args);
        } else {
                fwprintf(stderr, L"Unexpected program name '%s', must be '%s' or '%s'\n", 
                                   prog.c_str(), devnode, classfilter);

                return EXIT_FAILURE;
        }

        app.require_subcommand(1);
        CLI11_PARSE(app, argc, argv);
}
