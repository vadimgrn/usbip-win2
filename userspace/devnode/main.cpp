/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <regstr.h>
#include <ks.h>

#include <libusbip\format_message.h>

#include <libusbip\src\hkey.h>
#include <libusbip\src\setupapi.h>
#include <libusbip\src\strconv.h>
#include <libusbip\src\file_ver.h>

#include <CLI11\CLI11.hpp>

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
};

struct classfilter_args
{
        std::wstring infpath;
        std::wstring section_name;
};

using command_f = std::function<bool()>;

auto pack(command_f cmd) 
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
        auto msg_id = HRESULT_FROM_SETUPAPI(err);
        auto msg = wformat_message(msg_id);
        fwprintf(stderr, L"%S(%s) error %#lx %s\n", api, str, err, msg.c_str());
}

auto get_version(_In_ const wchar_t *program)
{
        win::FileVersion fv(program);
        auto ver = fv.GetFileVersion();
        return wchar_to_utf8(ver); // CLI::narrow(ver)
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

        if (WCHAR name[MAX_CLASS_NAME_LEN]; SetupDiGetINFClass(infname, &guid, name, ARRAYSIZE(name), nullptr)) {
                class_name = name;
        } else {
                errmsg("SetupDiGetINFClass", infname);
                assert(guid == GUID_NULL);
        }

        return guid;
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

using device_visitor_f = std::function<bool(HDEVINFO di, SP_DEVINFO_DATA &dd)>;

DWORD enum_device_info(_In_ HDEVINFO di, _In_ const device_visitor_f &func)
{
        SP_DEVINFO_DATA	dd{ .cbSize = sizeof(dd) };

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
inline auto get_device_property_ex(Args&&... args)
{
        auto err = get_device_property(std::forward<Args>(args)...);
        if (err) {
                errmsg("SetupDiGetDeviceProperty", L"", err);
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
        std::wstring class_name;
        auto class_guid = get_class_guid(class_name, r.infpath.c_str());
        if (class_guid == GUID_NULL) {
                return false;
        }

        hdevinfo dev_list(SetupDiCreateDeviceInfoList(&class_guid, nullptr));
        if (!dev_list) {
                errmsg("SetupDiCreateDeviceInfoList", class_name.c_str());
                return false;
        }

        SP_DEVINFO_DATA dev_data{ .cbSize = sizeof(dev_data) };
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

        SP_DEVINSTALL_PARAMS params{ .cbSize = sizeof(params) };
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
        }

        if (reboot || RebootRequired) {
                prompt_reboot();
        }

        return ok;
}

auto uninstall_device(
        _In_ HDEVINFO di, _In_  SP_DEVINFO_DATA &dd, _In_ const devnode_remove_args &r, _Inout_ bool &reboot)
{
        DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
        std::vector<BYTE> prop(REGSTR_VAL_MAX_HCID_LEN);

        if (!get_device_property_ex(di, dd, DEVPKEY_Device_HardwareIds, type, prop) || prop.empty()) {
                return false;
        }

        assert(type == DEVPROP_TYPE_STRING_LIST);
        
        if (as_wstring_view(prop) != r.hwid) {
                //
        } else if (r.dry_run) {
                prop.resize(MAX_DEVICE_ID_LEN);
                if (get_device_property_ex(di, dd, DEVPKEY_Device_InstanceId, type, prop) && !prop.empty()) {
                        assert(type == DEVPROP_TYPE_STRING);
                        auto id = as_wstring_view(prop);
                        wprintf(L"%s\n", id.data());
                }
        } else if (BOOL NeedReboot{}; !DiUninstallDevice(nullptr, di, &dd, 0, &NeedReboot)) {
                errmsg("DiUninstallDevice");
        } else if (NeedReboot) {
                reboot = true;
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
auto remove_devnode(_In_ devnode_remove_args &r)
{
        auto enumerator = r.enumerator.empty() ? nullptr : r.enumerator.c_str();

        hdevinfo di(SetupDiGetClassDevs(nullptr, enumerator, nullptr, DIGCF_ALLCLASSES));
        if (!di) {
                errmsg("SetupDiGetClassDevs");
                return false;
        }

        r.hwid = make_hwid(std::move(r.hwid)); // DEVPKEY_Device_HardwareIds is DEVPROP_TYPE_STRING_LIST

        bool reboot{};
        auto f = [&r, &reboot] (auto di, auto &dd) { return uninstall_device(di, dd, r, reboot); };

        if (auto err = enum_device_info(di.get(), f)) {
                errmsg("SetupDiEnumDeviceInfo", L"", err);
        }
                
        if (reboot) {
                prompt_reboot();
        }

        return true;
}

/*
 * Performs CopyFiles, DelFiles, RenFiles sections that are listed by an Install section.
 */
auto install_files(_In_ HINF hinf, _In_ const std::wstring &section_name, _In_ bool install)
{
        HspFileQ fileq(SetupOpenFileQueue());
        if (!fileq) {
                errmsg("SetupOpenFileQueue");
                return false;
        }

        if (UINT flags = install ? SP_COPY_IN_USE_NEEDS_REBOOT | SP_COPY_NOSKIP : 0;
            !SetupInstallFilesFromInfSection(hinf, nullptr, fileq.get(), section_name.c_str(), nullptr, flags)) {
                errmsg("SetupInstallFilesFromInfSection", section_name.c_str());
                return false;
        }

        using ctx_t = std::unique_ptr<void, decltype(SetupTermDefaultQueueCallback)&>;

        ctx_t ctx(SetupInitDefaultQueueCallback(nullptr), SetupTermDefaultQueueCallback);
        if (!ctx) {
                errmsg("SetupInitDefaultQueueCallback");
                return false;
        }

        if (!SetupCommitFileQueue(nullptr, fileq.get(), SetupDefaultQueueCallback, ctx.get())) {
                errmsg("SetupCommitFileQueue");
                return false;
        }

        return true;
}

/*
 * Performs service installation and deletion operations that are specified 
 * in the Service Install sections listed in the Service section of an INF file.
 */
auto install_services(
        _Inout_ bool &reboot_required, _In_ HINF hinf, _In_ std::wstring section_name, _In_ bool install)
{
        section_name += L".Services";

        if (SetLastError(ERROR_SUCCESS);
            !SetupInstallServicesFromInfSection(hinf, section_name.c_str(), install ? 0 : SPSVCINST_STOPSERVICE)) {
                errmsg("SetupInstallServicesFromInfSection", section_name.c_str());
                return false;
        }

        reboot_required = GetLastError() == ERROR_SUCCESS_REBOOT_REQUIRED;
        return true;
}

/*
 * Perform registry operations AddReg, DelReg in the Install section being processed.
 */
auto install_registry(_In_ HINF hinf, _In_ const classfilter_args &r)
{
        std::wstring class_name;
        auto class_guid = get_class_guid(class_name, r.infpath.c_str());
        if (class_guid == GUID_NULL) {
                return false; 
        }

        HKey key(SetupDiOpenClassRegKey(&class_guid, KEY_WRITE));
        if (!key) {
                errmsg("SetupDiOpenClassRegKey", class_name.c_str());
                return false;
        }

        if (!SetupInstallFromInfSection(nullptr, hinf, r.section_name.c_str(), SPINST_REGISTRY, key.get(),
                        nullptr, // SourceRootPath
                        0, // CopyFlags if Flags includes SPINST_FILES
                        nullptr, // MsgHandler
                        nullptr, // Context
                        nullptr, // DeviceInfoSet
                        nullptr)) { // DeviceInfoData
                errmsg("SetupInstallFromInfSection", r.section_name.c_str());
                return false;
        }

        return true;
}

/*
 * Install a class filter driver.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/install/installing-a-filter-driver
 */
auto classfilter_install(_In_ const classfilter_args &r, _In_ bool install)
{
        HInf inf(SetupOpenInfFile(r.infpath.c_str(), nullptr, INF_STYLE_WIN4, nullptr));
        if (!inf) {
                errmsg("SetupOpenInfFile", r.infpath.c_str());
                return false;
        }

        bool reboot_required{};
        auto ok = true;

        if (install) {
                ok = install_files(inf.get(), r.section_name, install) &&
                     install_services(reboot_required, inf.get(), r.section_name, install) &&
                     install_registry(inf.get(), r);
        } else { // ignore errors
                ok &= install_registry(inf.get(), r);
                ok &= install_services(reboot_required, inf.get(), r.section_name, install);
                ok &= install_files(inf.get(), r.section_name, install);
        }

        if (reboot_required) {
                prompt_reboot();
        }
        
        return ok;
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

void add_devnode_cmds(_In_ CLI::App &app)
{
        add_devnode_install_cmd(app);
        add_devnode_remove_cmd(app);
}

void add_classfilter_cmds(_In_ CLI::App &app)
{
        static classfilter_args r;
        auto &install = "install";

        for (auto action: {install, "uninstall"}) {
                auto cmd = app.add_subcommand(action, std::string(action) + " class filter driver");

                cmd->add_option("infpath", r.infpath, "Path to the driver's .inf file")
                        ->check(CLI::ExistingFile)
                        ->required();

                cmd->add_option("section_name", r.section_name, 
                        "The name of the " + std::string(action) + " section that lists CopyFiles/DelFiles/RenFiles sections")
                        ->required();

                auto f = [&r = r, install = action == install] { return classfilter_install(r, install); };
                cmd->callback(pack(std::move(f)));
        }
}

} // namespace


int wmain(_In_ int argc, _Inout_ wchar_t* argv[])
{
        CLI::App app("usbip2 drivers installation utility");
        
        app.option_defaults()->always_capture_default();
        app.set_version_flag("-V,--version", get_version(*argv));

        auto &devnode = L"devnode";
        auto &classfilter = L"classfilter";

        if (auto program = std::filesystem::path(*argv).stem(); program == devnode) {
                add_devnode_cmds(app);
        } else if (program == classfilter) {
                add_classfilter_cmds(app);
        } else {
                fwprintf(stderr, L"Program name must be '%s' or '%s', not '%s'\n", 
                                   devnode, classfilter, program.c_str());

                return EXIT_FAILURE;
        }

        app.require_subcommand(1);
        CLI11_PARSE(app, argc, argv);
}
