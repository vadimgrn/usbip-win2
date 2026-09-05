/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <shlwapi.h>
#include <cfgmgr32.h>
#include <newdev.h>

#include <libusbip/format_message.h>

#include <libusbip/src/setupapi.h>
#include <libusbip/src/strconv.h>
#include <libusbip/src/file_ver.h>

#include <CLI11/CLI11.hpp>
#include <filesystem>
#include <optional>
#include <print>
#include <utility>

#include <initguid.h>
#include <devpkey.h>

/*
 * See: devcon utility
 * https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
 */

namespace
{

using namespace usbip;

template <typename F>
class scope_exit
{
public:
        explicit scope_exit(F &&f) : m_f(std::forward<F>(f)) {}
        ~scope_exit() { if (m_active) m_f(); }
        void release() noexcept { m_active = false; }

        scope_exit(const scope_exit&) = delete;
        scope_exit& operator=(const scope_exit&) = delete;

private:
        F m_f;
        bool m_active{true};
};

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
};

struct remove_stats
{
        int matched{};
        int removed{};
};

template <typename F>
auto pack(F &&cmd) 
{
        return [cmd = std::forward<F>(cmd)] {
                if (!cmd()) {
                        throw CLI::RuntimeError(EXIT_FAILURE);
                }
        };
}

void errmsg(_In_ LPCSTR api, _In_ LPCWSTR str = L"", _In_ DWORD err = GetLastError())
{
        auto mod = GetModuleHandle(L"setupapi.dll");
        auto msg = wformat_message(mod, err);
        auto u8_msg = wchar_to_utf8_or(msg);

        if (*str) {
                std::println(stderr, "{}({}) error {:#x} {}", api, wchar_to_utf8_or(str), err, u8_msg);
        } else {
                std::println(stderr, "{} error {:#x} {}", api, err, u8_msg);
        }

        if (err == ERROR_ACCESS_DENIED) {
                std::println(stderr, "Administrator privileges are required.");
        }
}

auto get_version()
{
        win::FileVersion fv;
        auto ver = fv.GetFileVersion();
        return wchar_to_utf8_or(ver);
}

/*
 * @return REG_MULTI_SZ 
 */
auto make_hwid(_In_ std::wstring_view hwid)
{
        std::wstring s;
        s.reserve(hwid.size() + 2);
        s.append(hwid);
        s.push_back(L'\0'); // first string
        s.push_back(L'\0'); // end of the list
        return s;
}

struct device_class
{
        GUID guid{GUID_NULL};
        std::wstring name;
};

std::optional<device_class> get_class_info(_In_ PCWSTR infname)
{
        device_class cls;
        WCHAR name[MAX_CLASS_NAME_LEN];

        if (SetupDiGetINFClass(infname, &cls.guid, name, static_cast<DWORD>(std::size(name)), nullptr)) {
                cls.name = name;
                return cls;
        }

        errmsg("SetupDiGetINFClass", infname);
        return std::nullopt;
}

void remind_reboot() noexcept
{
        std::println("Reboot is required to finish setup.");
}

template <typename Visitor>
DWORD enum_device_info(_In_ HDEVINFO di, Visitor &&func)
{
        SP_DEVINFO_DATA dd{};
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
                    SetupDiGetDeviceProperty(di, &dd, &key, &type, prop.data(), static_cast<DWORD>(prop.size()), &actual, 0)) {
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

std::vector<std::wstring> get_device_hardware_ids(_In_ HDEVINFO di, _In_ SP_DEVINFO_DATA &dd)
{
        DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
        std::vector<BYTE> buf;

        if (auto err = get_device_property(di, dd, DEVPKEY_Device_HardwareIds, type, buf)) {
                if (err != ERROR_NOT_FOUND) {
                        errmsg("SetupDiGetDeviceProperty", L"HardwareIds", err);
                }
                return {};
        }

        if (type != DEVPROP_TYPE_STRING_LIST || buf.size() < sizeof(wchar_t)) {
                return {};
        }

        auto ws_view = std::wstring_view(reinterpret_cast<const wchar_t*>(buf.data()), buf.size() / sizeof(wchar_t));
        return split_multi_sz(ws_view);
}

std::optional<std::wstring> get_device_instance_id(_In_ HDEVINFO di, _In_ SP_DEVINFO_DATA &dd)
{
        DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
        std::vector<BYTE> buf;

        if (auto err = get_device_property(di, dd, DEVPKEY_Device_InstanceId, type, buf)) {
                if (err != ERROR_NOT_FOUND) {
                        errmsg("SetupDiGetDeviceProperty", L"InstanceId", err);
                }
                return std::nullopt;
        }

        if (type != DEVPROP_TYPE_STRING || buf.size() < sizeof(wchar_t)) {
                return std::nullopt;
        }

        auto c_str = reinterpret_cast<const wchar_t*>(buf.data());
        auto len = wcsnlen_s(c_str, buf.size() / sizeof(wchar_t));
        return std::wstring(c_str, len);
}

/*
 * @param infpath must be an absolute path
 * @see devcon, cmd/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto install_devnode_and_driver(_In_ const devnode_install_args &r)
{
        auto cls = get_class_info(r.infpath.c_str());
        if (!cls) {
                return false;
        }

        hdevinfo dev_list(SetupDiCreateDeviceInfoList(&cls->guid, nullptr));
        if (!dev_list) {
                errmsg("SetupDiCreateDeviceInfoList", cls->name.c_str());
                return false;
        }

        SP_DEVINFO_DATA dev_data{};
        dev_data.cbSize = sizeof(dev_data);

        if (!SetupDiCreateDeviceInfo(dev_list.get(), cls->name.c_str(), &cls->guid, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
                errmsg("SetupDiCreateDeviceInfo");
                return false;
        }

        auto id = make_hwid(r.hwid);
        auto id_sz = static_cast<DWORD>(id.size() * sizeof(wchar_t));

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                                              reinterpret_cast<const BYTE*>(id.data()), id_sz)) {
                errmsg("SetupDiSetDeviceRegistryProperty");
                return false;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, dev_list.get(), &dev_data)) {
                errmsg("SetupDiCallClassInstaller");
                return false;
        }

        scope_exit rollback([dev_list = dev_list.get(), &dev_data] {
                SetupDiCallClassInstaller(DIF_REMOVE, dev_list, &dev_data);
        });

        SP_DEVINSTALL_PARAMS params{};
        params.cbSize = sizeof(params);

        if (!SetupDiGetDeviceInstallParams(dev_list.get(), &dev_data, &params)) {
                errmsg("SetupDiGetDeviceInstallParams");
                return false;
        }
        bool reboot = params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART);

        // the same as "pnputil /add-driver usbip2_ude.inf /install"

        BOOL RebootRequired{};
        bool ok = UpdateDriverForPlugAndPlayDevices(nullptr, r.hwid.c_str(), r.infpath.c_str(), INSTALLFLAG_FORCE, &RebootRequired);
        if (!ok) {
                errmsg("UpdateDriverForPlugAndPlayDevices");
                return false;
        }

        rollback.release();

        if (reboot || RebootRequired) {
                remind_reboot();
        }

        return true;
}

auto uninstall_device(
        _In_ HDEVINFO di, _In_ SP_DEVINFO_DATA &dd, _In_ const devnode_remove_args &r, _Inout_ remove_stats &stats, _Inout_ bool &reboot)
{
        auto ids = get_device_hardware_ids(di, dd);
        if (ids.empty()) {
                return false;
        }

        auto found = std::ranges::any_of(ids, [&r] (const auto &id) {
                return PathMatchSpec(id.c_str(), r.hwid.c_str());
        });

        if (!found) {
                return false;
        }

        ++stats.matched;

        if (r.dry_run) {
                if (auto id = get_device_instance_id(di, dd)) {
                        std::println("{}", wchar_to_utf8_or(*id));
                }
        } else if (BOOL NeedReboot{}; !DiUninstallDevice(nullptr, di, &dd, 0, &NeedReboot)) {
                errmsg("DiUninstallDevice");
        } else {
                ++stats.removed;
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
auto remove_devnode(_In_ const devnode_remove_args &r)
{
        auto enumerator = r.enumerator.empty() ? nullptr : r.enumerator.c_str();

        hdevinfo di(SetupDiGetClassDevs(nullptr, enumerator, nullptr, DIGCF_ALLCLASSES));
        if (!di) {
                errmsg("SetupDiGetClassDevs");
                return false;
        }

        remove_stats stats;
        bool reboot{};
        auto f = [&r, &stats, &reboot] (auto di, auto &dd) { return uninstall_device(di, dd, r, stats, reboot); };

        if (auto err = enum_device_info(di.get(), f)) {
                errmsg("SetupDiEnumDeviceInfo", L"", err);
        }
                
        if (reboot) {
                remind_reboot();
        }

        if (r.dry_run) {
                std::println("{} matching device(s) found.", stats.matched);
        } else {
                std::println("{} device(s) were removed.", stats.removed);
        }

        return stats.matched > 0;
}

void add_devnode_install_cmd(_In_ CLI::App &app)
{
        static devnode_install_args r;
        auto cmd = app.add_subcommand("install", "Install a device node and its driver");

        cmd->add_option("infpath", r.infpath, "Path to the driver's .inf file")
                ->check(CLI::ExistingFile)
                ->required();

        cmd->add_option("hwid", r.hwid, "Hardware Id of the device")->required();

        auto f = [&r = r] 
        { 
                r.infpath = std::filesystem::absolute(r.infpath).wstring();
                return install_devnode_and_driver(r); 
        };
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
        try {
                CLI::App app("usbip2 drivers installation utility");
                
                app.option_defaults()->always_capture_default();
                app.set_version_flag("-V,--version", get_version());

                add_devnode_install_cmd(app);
                add_devnode_remove_cmd(app);

                app.require_subcommand(1);
                CLI11_PARSE(app, argc, argv);
        } catch (std::exception &e) {
                std::println(stderr, "exception: {}", e.what());
                return EXIT_FAILURE;
        }
}
