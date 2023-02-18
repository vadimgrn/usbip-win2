/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "resource.h"

#include <libusbip\output.h>
#include <libusbip\win_socket.h>
#include <libusbip\file_ver.h>
#include <libusbip\usb_ids.h>
#include <libusbip\strconv.h>
#include <libusbip\hmodule.h>

#include <resources\messages.h>

#include <spdlog\spdlog.h>
#include <spdlog\sinks\stdout_color_sinks.h>

#include <libusbip\CLI11.hpp>

namespace
{

using namespace usbip;

auto get_ids_data()
{
	win::Resource r(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_USB_IDS), RT_RCDATA);
	assert(r);
	return r.str();
}

auto get_version(_In_ const wchar_t *program)
{
	win::FileVersion fv(program);
	auto ver = fv.GetFileVersion();
	return wchar_to_utf8(ver); // CLI::narrow
}

auto pack(command_t cmd, void *p) 
{
	return [cmd, p] { 
		if (!cmd(p)) {
			exit(EXIT_FAILURE); // throw CLI::RuntimeError(EXIT_FAILURE);
		}
	};
}

void add_cmd_attach(CLI::App &app)
{
	static attach_args r;
	auto cmd = app.add_subcommand("attach", "Attach a remote USB device");

	cmd->add_option("-r,--remote", r.remote, "Hostname/IP of a USB/IP server with exported USB devices")->required();
	cmd->add_option("-b,--bus-id", r.busid, "Bus Id of the USB device on a server")->required();
	cmd->add_flag("-t,--terse", r.terse, "Show port number as a result");

	cmd->callback(pack(cmd_attach, &r));
}

void add_cmd_detach(CLI::App &app)
{
	static detach_args r;
	auto cmd = app.add_subcommand("detach", "Detach a remote USB device");

	cmd->add_option("-p,--port", r.port, "Hub port number the device is plugged in, -1 or zero means ALL ports")
		->check(CLI::Range(-1, int(vhci::TOTAL_PORTS)))
		->required();

	cmd->callback(pack(cmd_detach, &r));
}

void add_cmd_list(CLI::App &app)
{
	static list_args r;
	auto cmd = app.add_subcommand("list", "List exportable USB devices");

	cmd->add_option("-r,--remote", r.remote, "List exportable devices on a remote")->required();
	cmd->callback(pack(cmd_list, &r));
}

void add_cmd_port(CLI::App &app)
{
	static port_args r;
	auto cmd = app.add_subcommand("port", "Show imported USB devices");

	cmd->add_option("number", r.ports, "Hub port number")
		->check(CLI::Range(1, int(vhci::TOTAL_PORTS)))
		->expected(1, vhci::TOTAL_PORTS);

	cmd->callback(pack(cmd_port, &r));
}

void init(CLI::App &app, const wchar_t *program)
{
	app.set_version_flag("-V,--version", get_version(program));

	app.add_flag("-d,--debug", 
		     [] (auto) { spdlog::set_level(spdlog::level::debug); }, "Debug output");

	app.add_option("-t,--tcp-port", global_args.tcp_port, "TCP/IP port number of USB/IP server")
		->check(CLI::Range(1024, USHRT_MAX))
		->capture_default_str();
}

auto &msgtable_dll = L"resources"; // resource-only DLL that contains RT_MESSAGETABLE

auto& get_resource_module()
{
	static HModule mod(LoadLibraryEx(msgtable_dll, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR));
	return mod;
}

void init_spdlog()
{
	auto logger = spdlog::stderr_color_st("stderr");
	libusbip::logger = [logger] (auto msg) { logger->debug(msg); };

	set_default_logger(std::move(logger));
	spdlog::set_pattern("%^%l%$: %v");
}

} // namespace


std::string usbip::GetLastErrorMsg(unsigned long msg_id)
{
	static_assert(sizeof(msg_id) == sizeof(UINT32));
	static_assert(std::is_same_v<decltype(msg_id), DWORD>);

	if (msg_id == ~0UL) {
		msg_id = GetLastError();
	}

	auto &mod = get_resource_module();
	return format_message(mod.get(), msg_id);
}

const UsbIds& usbip::get_ids()
{
	static UsbIds ids(get_ids_data());
	assert(ids);
	return ids;
}

int wmain(int argc, wchar_t *argv[])
{
	init_spdlog();

	if (!get_resource_module()) {
		auto err = GetLastError();
		spdlog::critical(L"can't load '{}.dll', error {:#x} {}", msgtable_dll, err, wformat_message(err));
		return EXIT_FAILURE;
	}

	InitWinSock2 ws2;
	if (!ws2) {
		spdlog::critical("can't initialize Windows Sockets 2, {}", GetLastErrorMsg());
		return EXIT_FAILURE;
	}

	CLI::App app("USB/IP client");
	init(app, *argv);

	add_cmd_attach(app);
	add_cmd_detach(app);
	add_cmd_list(app);
	add_cmd_port(app);

	app.require_subcommand(1);
	CLI11_PARSE(app, argc, argv);
}
