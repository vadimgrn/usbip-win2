/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <shlwapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <regstr.h>

#include <libusbip/format_message.h>

#include <libusbip/src/hkey.h>
#include <libusbip/src/setupapi.h>
#include <libusbip/src/strconv.h>
#include <libusbip/src/file_ver.h>

#include <CLI11/CLI11.hpp>
#include <filesystem>

#include <initguid.h>
#include <devpkey.h>

/*
 * See: devcon utility
 * https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
 */

namespace
{

using namespace usbip;

struct devnode_install_args
{
        std::wstring infpath;
        std::wstring hwid;
};

struct devnode_remove_args
{
        std::wstring hwid;
        std::wstring enumerator;
        bool dry_run{};
        int matched_count{};
        int removed_count{};
};

using command_f = std::function<bool()>;

auto pack(command_f cmd) 
{
        return [cmd = std::move(cmd)] 
        {
                if (!cmd()) {
                        throw CLI::RuntimeError(EXIT_FAILURE);
                }
        };
}

void errmsg(_In_ LPCSTR api, _In_ LPCWSTR str = L"", _In_ DWORD err = GetLastError())
{
        auto msg_id = HRESULT_FROM_SETUPAPI(err);
        auto msg = wformat_message(msg_id);
        fwprintf(stderr, L"%S(%s) error %#lx %s\n", api, str, err, msg.c_str());
        if (err == ERROR_ACCESS_DENIED) {
                fwprintf(stderr, L"Administrator privileges are required.\n");
        }
}

auto get_version()
{
        wchar_t program[MAX_PATH];
        if (GetModuleFileName(nullptr, program, static_cast<DWORD>(std::size(program)))) {
                win::FileVersion fv(program);
                auto ver = fv.GetFileVersion();
                return wchar_to_utf8_or(ver);
        }
        return std::string{};
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

auto get_class_guid(_Inout_ std::wstring &class_name, _In_ PCWSTR infname)
{
        auto guid = GUID_NULL;

        if (WCHAR name[MAX_CLASS_NAME_LEN]; SetupDiGetINFClass(infname, &guid, name, std::size(name), nullptr)) {
                class_name = name;
        } else {
                errmsg("SetupDiGetINFClass", infname);
                assert(guid == GUID_NULL);
        }

        return guid;
}

void remind_reboot() noexcept
{
        wprintf(L"Reboot is required to finish setup.\n");
}

using device_visitor_f = std::function<bool(HDEVINFO di, SP_DEVINFO_DATA &dd)>;

DWORD enum_device_info(_In_ HDEVINFO di, _In_ const device_visitor_f &func)
{
        SP_DEVINFO_DATA	dd{};
        dd.cbSize = sizeof(dd);

        for (DWORD i = 0; ; ++i) {
                if (SetupDiEnumDeviceInfo(di, i, &dd)) {
                        if (func(di, dd)) {
                                return ERROR_SUCCESS;
                        }
                } else if (auto err = GetLastError(); err == ERROR_NO_MORE_ITEMS) {
                        return ERROR_SUCCESS;
                } else {
                        return err;
                }
        }
}

DWORD get_device_property(
        _In_ HDEVINFO di, _In_ SP_DEVINFO_DATA &dd, 
        _In_ const DEVPROPKEY &key,
        _Out_ DEVPROPTYPE &type,
        _Inout_ std::vector<BYTE> &prop)
{
        for (;;) {
                if (DWORD actual{}; // bytes
                    SetupDiGetDeviceProperty(di, &dd, &key, &type, prop.data(), DWORD(prop.size()), &actual, 0)) {
                        prop.resize(actual);
                        return ERROR_SUCCESS;
                } else if (auto err = GetLastError(); err == ERROR_INSUFFICIENT_BUFFER) {
                        prop.resize(actual);
                } else {
                        prop.clear();
                        return err;
                }
        }
}

template<typename... Args>
inline auto get_device_property_ex(const wchar_t *prop_name, Args&&... args)
{
        auto err = get_device_property(std::forward<Args>(args)...);
        if (err && err != ERROR_NOT_FOUND) {
                errmsg("SetupDiGetDeviceProperty", prop_name, err);
        }
        return !err;
}

inline auto as_wstring_view(_In_ std::vector<BYTE> &v) noexcept
{
        assert(!(v.size() % sizeof(wchar_t)));
        return std::wstring_view(reinterpret_cast<wchar_t*>(v.data()), v.size()/sizeof(wchar_t));
}

/*
 * @param infpath must be an absolute path
 * @see devcon, cmd/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto install_devnode_and_driver(_In_ const devnode_install_args &r)
{
        auto infpath = std::filesystem::absolute(r.infpath).wstring();

        std::wstring class_name;
        auto class_guid = get_class_guid(class_name, infpath.c_str());
        if (class_guid == GUID_NULL) {
                return false;
        }

        hdevinfo dev_list(SetupDiCreateDeviceInfoList(&class_guid, nullptr));
        if (!dev_list) {
                errmsg("SetupDiCreateDeviceInfoList", class_name.c_str());
                return false;
        }

        SP_DEVINFO_DATA dev_data{};
        dev_data.cbSize = sizeof(dev_data);

        if (!SetupDiCreateDeviceInfo(dev_list.get(), class_name.c_str(), &class_guid, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
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

        SP_DEVINSTALL_PARAMS params{};
        params.cbSize = sizeof(params);

        if (!SetupDiGetDeviceInstallParams(dev_list.get(), &dev_data, &params)) {
                errmsg("SetupDiGetDeviceInstallParams");
                return false;
        }
        bool reboot = params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART);

        // the same as "pnputil /add-driver usbip2_ude.inf /install"

        BOOL RebootRequired{};
        bool ok = UpdateDriverForPlugAndPlayDevices(nullptr, r.hwid.c_str(), infpath.c_str(), INSTALLFLAG_FORCE, &RebootRequired);
        if (!ok) {
                errmsg("UpdateDriverForPlugAndPlayDevices");
                SetupDiCallClassInstaller(DIF_REMOVE, dev_list.get(), &dev_data);
                return false;
        }

        if (reboot || RebootRequired) {
                remind_reboot();
        }

        return true;
}

auto uninstall_device(
        _In_ HDEVINFO di, _In_ SP_DEVINFO_DATA &dd, _Inout_ devnode_remove_args &r, _Inout_ bool &reboot)
{
        DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
        std::vector<BYTE> prop(REGSTR_VAL_MAX_HCID_LEN);

        if (!get_device_property_ex(L"HardwareIds", di, dd, DEVPKEY_Device_HardwareIds, type, prop) || prop.empty()) {
                return false;
        }

        if (type != DEVPROP_TYPE_STRING_LIST) {
                return false;
        }
        
        auto ids = split_multi_sz(as_wstring_view(prop));

        auto found = std::ranges::any_of(ids, [&r] (const auto &id) {
                return PathMatchSpec(id.c_str(), r.hwid.c_str());
        });

        if (!found) {
                return false;
        }

        ++r.matched_count;

        if (r.dry_run) {
                prop.resize(MAX_DEVICE_ID_LEN * sizeof(wchar_t));
                if (get_device_property_ex(L"InstanceId", di, dd, DEVPKEY_Device_InstanceId, type, prop) && !prop.empty()) {
                        assert(type == DEVPROP_TYPE_STRING);
                        auto id = reinterpret_cast<const wchar_t*>(prop.data());
                        wprintf(L"%ls\n", id);
                }
        } else if (BOOL NeedReboot{}; !DiUninstallDevice(nullptr, di, &dd, 0, &NeedReboot)) {
                errmsg("DiUninstallDevice");
        } else {
                ++r.removed_count;
                if (NeedReboot) {
                        reboot = true;
                }
        }

        return false;
}

/*
 * pnputil /remove-device /deviceid <HWID>
 * a) /remove-device is available since Windows 10 version 2004
 * b) /deviceid flag is available since Windows 11 version 21H2
 * 
 * DIGCF_ALLCLASSES is used to find devices without a driver (Class = Unknown or Class = NoDriver).
 *
 * @see devcon, cmd/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto remove_devnode(_Inout_ devnode_remove_args &r)
{
        auto enumerator = r.enumerator.empty() ? nullptr : r.enumerator.c_str();

        hdevinfo di(SetupDiGetClassDevs(nullptr, enumerator, nullptr, DIGCF_ALLCLASSES));
        if (!di) {
                errmsg("SetupDiGetClassDevs");
                return false;
        }

        bool reboot{};
        auto f = [&r, &reboot] (auto di, auto &dd) { return uninstall_device(di, dd, r, reboot); };

        if (auto err = enum_device_info(di.get(), f)) {
                errmsg("SetupDiEnumDeviceInfo", L"", err);
        }
                
        if (reboot) {
                remind_reboot();
        }

        if (r.dry_run) {
                wprintf(L"%d matching device(s) found.\n", r.matched_count);
        } else {
                wprintf(L"%d device(s) were removed.\n", r.removed_count);
        }

        return r.matched_count > 0;
}

void add_devnode_install_cmd(_In_ CLI::App &app)
{
        static devnode_install_args r;
        auto cmd = app.add_subcommand("install", "Install a device node and its driver");

        cmd->add_option("infpath", r.infpath, "Path to the driver's .inf file")
                ->check(CLI::ExistingFile)
                ->required();

        cmd->add_option("hwid", r.hwid, "Hardware Id of the device")->required();

        auto f = [&r = r] { return install_devnode_and_driver(r); };
        cmd->callback(pack(std::move(f)));
}

void add_devnode_remove_cmd(_In_ CLI::App &app)
{
        static devnode_remove_args r;
        auto cmd = app.add_subcommand("remove", "Uninstall a device and remove its device nodes");

        cmd->add_option("hwid", r.hwid, "Hardware Id of the device")->required();
        cmd->add_option("enumerator", r.enumerator, "An identifier of a Plug and Play enumerator");

        cmd->add_flag("-n,--dry-run", r.dry_run, 
                      "Print InstanceId of devices that will be removed instead of removing them");

        auto f = [&r = r] { return remove_devnode(r); };
        cmd->callback(pack(std::move(f)));
}

} // namespace


int wmain(_In_ int argc, _Inout_ wchar_t* argv[])
{
        CLI::App app("usbip2 drivers installation utility");
        
        app.option_defaults()->always_capture_default();
        app.set_version_flag("-V,--version", get_version());

        add_devnode_install_cmd(app);
        add_devnode_remove_cmd(app);

        app.require_subcommand(1);
        CLI11_PARSE(app, argc, argv);
}
