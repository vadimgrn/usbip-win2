/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <regstr.h>
#include <combaseapi.h>

#include <libusbip\hkey.h>
#include <libusbip\setupdi.h>
#include <libusbip\file_ver.h>
#include <libusbip\strconv.h>

#include <libusbip\CLI11.hpp>

/*
 * See: devcon utility
 * https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
 */

namespace
{

using namespace usbip;

struct devnode_args
{
        std::wstring infpath;
        std::wstring hwid;
};

struct classfilter_args
{
        std::string_view level;
        std::wstring class_guid;
        std::wstring driver_name;
};

auto &opt_upper = "upper";

using command_t = std::function<bool()>;

auto pack(command_t cmd) 
{
        return [cmd = std::move(cmd)] 
        {
                if (!cmd()) {
                        exit(EXIT_FAILURE); // throw CLI::RuntimeError(EXIT_FAILURE);
                }
        };
}

void errmsg(_In_ LPCSTR api, _In_ LPCWSTR str = L"", _In_ DWORD err = GetLastError())
{
        auto msg = wformat_message(err);
        fwprintf(stderr, L"%S(%s) error %#lx %s", api, str, err, msg.c_str());
}

auto get_version(_In_ const wchar_t *program)
{
        FileVersion fv(program);
        auto ver = fv.GetFileVersion();
        return wchar_to_utf8(ver); // CLI::narrow
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
                        errmsg("RegGetValue", val_name, err);
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
                errmsg("SetupPromptReboot");
        }
}

/*
 * @param infpath must be an absolute path
 * @see devcon, cmd/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto install_devnode_and_driver(_In_ devnode_args &r)
{
        GUID ClassGUID;
        WCHAR ClassName[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClass(r.infpath.c_str(), &ClassGUID, ClassName, ARRAYSIZE(ClassName), 0)) {
                errmsg("SetupDiGetINFClass", r.infpath.c_str());
                return false;
        }

        auto dev_list = hdevinfo(SetupDiCreateDeviceInfoList(&ClassGUID, nullptr));
        if (!dev_list) {
                errmsg("SetupDiCreateDeviceInfoList");
                return false;
        }

        SP_DEVINFO_DATA dev_data{ sizeof(dev_data) };
        if (!SetupDiCreateDeviceInfo(dev_list.get(), ClassName, &ClassGUID, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
                errmsg("SetupDiCreateDeviceInfo");
                return false;
        }

        auto id = make_hwid(r.hwid);
        auto id_sz = DWORD(id.length()*sizeof(id[0]));

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                                              reinterpret_cast<const BYTE*>(id.data()), id_sz)) {
                errmsg("SetupDiSetDeviceRegistryProperty");
                return false;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, dev_list.get(), &dev_data)) {
                errmsg("SetupDiCallClassInstaller");
                return false;
        }

        SP_DEVINSTALL_PARAMS params{ .cbSize = sizeof(params) };
        if (!SetupDiGetDeviceInstallParams(dev_list.get(), &dev_data, &params)) {
                errmsg("SetupDiGetDeviceInstallParams");
                return false;
        }
        auto reboot_required = params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART);

        // the same as "pnputil /add-driver usbip2_vhci.inf /install"

        BOOL RebootRequired;
        if (!UpdateDriverForPlugAndPlayDevices(nullptr, r.hwid.c_str(), r.infpath.c_str(), INSTALLFLAG_FORCE, &RebootRequired)) {
                errmsg("UpdateDriverForPlugAndPlayDevices");
                return false;
        }

        if (reboot_required || RebootRequired) {
                prompt_reboot();
        }

        return true;
}

/*
 * devcon classfilter usb upper ; query
 * devcon classfilter usb upper !usbip2_filter ; remove
 * @see devcon, cmdClassFilter
 */
auto classfilter(_In_ classfilter_args &r, _In_ bool add)
{
        GUID ClassGUID;
        if (auto clsid = r.class_guid.data(); auto err = CLSIDFromString(clsid, &ClassGUID)) {
                errmsg("CLSIDFromString", clsid, err);
                return false;
        }

        HKey key(SetupDiOpenClassRegKeyEx(&ClassGUID, KEY_QUERY_VALUE | KEY_SET_VALUE, DIOCR_INSTALLER, nullptr, nullptr));
        if (!key) {
                errmsg("SetupDiOpenClassRegKeyEx", r.class_guid.data());
                return false;
        }

        auto val_name = r.level == opt_upper ? REGSTR_VAL_UPPERFILTERS : REGSTR_VAL_LOWERFILTERS;
        std::vector<WCHAR> val(4096);
        if (!read_multi_z(key.get(), val_name, val)) {
                return false;
        }

        auto modified = add;
        auto filters = split_multi_sz(val.empty() ? nullptr : val.data(), r.driver_name, modified);
        if (add) {
                filters.emplace_back(r.driver_name);
        }

        if (!modified) {
                return true;
        }

        if (auto str = make_multi_sz(filters); 
            auto err = RegSetValueEx(key.get(), val_name, 0, REG_MULTI_SZ,
                                     reinterpret_cast<const BYTE*>(str.data()), 
                                     DWORD(str.length()*sizeof(str[0])))) {
                errmsg("RegSetValueEx", val_name, err);
                return false;
        }

        return true;
}

void add_devnode_cmds(_In_ CLI::App &app)
{
        static devnode_args r;
        auto cmd = app.add_subcommand("install", "Install a device node and its driver");

        cmd->add_option("infpath", r.infpath, "Path fo .inf file")
                ->check(CLI::ExistingFile)
                ->required();

        cmd->add_option("hwid", r.hwid, "Hardware Id of the device")->required();
        
        auto f = [r = &r] { return install_devnode_and_driver(*r); };
        cmd->callback(pack(std::move(f)));
}

void add_classfilter_cmds(_In_ CLI::App &app)
{
        static classfilter_args r;
        auto &cmd_add = "add";

        for (auto action: {cmd_add, "remove"}) {

                auto cmd = app.add_subcommand(action, std::string(action) + " class filter driver");

                cmd->add_option("Level", r.level)
                        ->check(CLI::IsMember({opt_upper, "lower"}))
                        ->required();

                cmd->add_option("ClassGuid", r.class_guid)->required();
                cmd->add_option("DriverName", r.driver_name, "Filter driver name")->required();

                auto f = [r = &r, add = action == cmd_add] { return classfilter(*r, add); };
                cmd->callback(pack(std::move(f)));
        }
}

} // namespace


int wmain(_In_ int argc, _Inout_ wchar_t* argv[])
{
        CLI::App app("usbip2 driver installation utility");
        app.set_version_flag("-V,--version", get_version(*argv));

        auto &devnode = L"devnode";
        auto &classfilter = L"classfilter";

        if (auto program = std::filesystem::path(*argv).stem(); program == devnode) {
                add_devnode_cmds(app);
        } else if (program == classfilter) {
                add_classfilter_cmds(app);
        } else {
                fwprintf(stderr, L"Unexpected program name '%s', must be '%s' or '%s'\n", 
                                   program.c_str(), devnode, classfilter);

                return EXIT_FAILURE;
        }

        app.require_subcommand(1);
        CLI11_PARSE(app, argc, argv);
}
