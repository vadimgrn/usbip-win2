/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "resource.h"

#include <libusbip\win_socket.h>
#include <libusbip\file_ver.h>
#include <libusbip\usb_ids.h>

#include <usbip\vhci.h>

#include <spdlog\spdlog.h>
#include <spdlog\sinks\stdout_color_sinks.h>

#include <libusbip\CLI11.hpp>

namespace
{

using namespace usbip;

auto get_ids_data()
{
	Resource r(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_USB_IDS), RT_RCDATA);
	assert(r);
	return r.str();
}

auto get_version(_In_ const wchar_t *program)
{
	FileVersion fv(program);
	auto ver = fv.GetFileVersion();
	return CLI::narrow(ver);
}

template<typename F, typename... Args>
inline auto pack(F f, Args&&... args) 
{
	return [f, &...args = std::forward<Args>(args)] 
	{ 
		if (auto err = f(std::forward<Args>(args)...)) {
			spdlog::debug("exit code {}", err);
			exit(err); // throw CLI::RuntimeError(err);
		}
	};
}

void add_cmd_attach(CLI::App &app, attach_args &r)
{
	auto cmd = app.add_subcommand("attach", "Attach a remote USB device");

	cmd->add_option("-r,--remote", r.remote, "Hostname/IP of a USB/IP server with exported USB devices")->required();
	cmd->add_option("-b,--bus-id", r.busid, "Bus Id of the USB device on a server")->required();
	cmd->add_flag("-t,--terse", r.terse, "Show port number as a result");

	cmd->callback(pack(cmd_attach, r));
}

void add_cmd_detach(CLI::App &app, detach_args &r)
{
	auto cmd = app.add_subcommand("detach", "Detach a remote USB device");

	cmd->add_option("-p,--port", r.port, "Hub port number the device is plugged in, -1 or zero means ALL ports")
		->check(CLI::Range(-1, int(vhci::TOTAL_PORTS)))
		->required();

	cmd->callback(pack(cmd_detach, r));
}

void add_cmd_list(CLI::App &app, list_args &r)
{
	auto cmd = app.add_subcommand("list", "List USB devices");
	cmd->add_option("-r,--remote", r.remote, "List exported devices on a remote")->required();

	cmd->callback(pack(cmd_list, r));
}

void add_cmd_port(CLI::App &app, port_args &r)
{
	auto cmd = app.add_subcommand("port", "List given hub port(s) for checking");

	cmd->add_option("number", r.ports, "Hub port number")
		->check(CLI::Range(1, int(vhci::TOTAL_PORTS)))
		->expected(1, vhci::TOTAL_PORTS);

	cmd->callback(pack(cmd_port, r));
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

} // namespace


const UsbIds& usbip::get_ids()
{
	static UsbIds ids(get_ids_data());
	assert(ids);
	return ids;
}

int wmain(int argc, wchar_t *argv[])
{
	set_default_logger(spdlog::stderr_color_st("stderr"));
	spdlog::set_pattern("%^%l%$: %v");

	InitWinSock2 ws2;
	if (!ws2) {
		spdlog::critical("cannot initialize winsock2");
		return EXIT_FAILURE;
	}

	CLI::App app("USB/IP client");
	init(app, *argv);
	
	attach_args attach_opts;
	add_cmd_attach(app, attach_opts);

	detach_args detach_opts;
	add_cmd_detach(app, detach_opts);

	list_args list_opts;
	add_cmd_list(app, list_opts);

	port_args port_opts;
	add_cmd_port(app, port_opts);

	app.require_subcommand(1);
	CLI11_PARSE(app, argc, argv);
}
