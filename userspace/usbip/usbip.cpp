/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "resource.h"

#include <libusbip/vhci.h>
#include <libusbip/output.h>
#include <libusbip/win_handle.h>
#include <libusbip/win_socket.h>
#include <libusbip/format_message.h>

#include <libusbip/src/usb_ids.h>
#include <libusbip/src/strconv.h>
#include <libusbip/src/file_ver.h>

#include <resources/messages.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <CLI11/CLI11.hpp>
#include <print>

namespace
{

using namespace usbip;

const auto MAX_HUB_PORTS = 255; // @see drivers/usbip_ude/vhci.cpp, set_usb_ports_cnt

constexpr auto &str_zero_copy = "zero-copy";
constexpr auto &str_low_latency = "low-latency";

auto get_ids_data()
{
	win::Resource r(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_USB_IDS), RT_RCDATA);
	assert(r);
	return r.str();
}

auto get_version()
{
        win::FileVersion fv;
        auto ver = fv.GetFileVersion();
        return wchar_to_utf8_or(ver);
}

template <typename Cmd, typename Args>
auto pack(Cmd &&cmd, const Args &args) 
{
	return [&cmd, &args] { 
		if (!cmd(args)) {
			throw CLI::RuntimeError(EXIT_FAILURE);
		}
	};
}

auto serial_validator(const std::string &serial)
{
        std::string s;
        if (!validate_device_serial(serial)) {
                s = get_last_error_msg();
        }
        return s;
}

void add_cmd_attach(CLI::App &app)
{
	static attach_args r;

	auto cmd = app.add_subcommand("attach", "Attach remote/persistent USB device(s)")
		->callback(pack(cmd_attach, r))
		->require_option(1);

	auto rem = cmd->add_option_group("Remote", "Attach remote USB device");

	rem->add_option("-r,--remote", r.remote, "Hostname/IP of a USB/IP server with exported USB devices")
		->required();	

	rem->add_option("-b,--bus-id", r.busid, "Bus Id of the USB device on a server")
		->required();	

	rem->add_option("--serial", r.serial, "device serial number")->check(serial_validator);
	rem->add_flag("-t,--terse", r.terse, "Show port number as a result");

        auto stop = rem->add_flag("-x,--stop", r.stop, "Stop attach attempts to this device");
        rem->add_flag("--once", r.once, "Do not start automatic attach attempts if cannot connect")->excludes(stop);

        auto receive = [&mode = r.recv_mode, low_lat = str_low_latency] (const auto &choice)
        {
                mode = choice == low_lat ? receive_mode::low_latency : receive_mode::zero_copy;
        };

        rem->add_option_function<std::string>("--receive-mode", receive, "How to receive data from the network")
                ->check(CLI::IsMember({str_zero_copy, str_low_latency}))
                ->default_str(str_zero_copy)
                ->excludes(stop);

	auto stop_all = cmd->add_option_group("Stop")
		->add_flag("-X,--stop-all", r.stop_all, "Stop all active attach attempts");

	stop->excludes(stop_all);

        cmd->add_option_group("Persistent", "Attach persistent USB device(s)")
                ->add_flag("-s,--stashed,--persistent", r.persistent, "Attach persistent device(s) stashed by 'port --stash'");
}

void add_cmd_detach(CLI::App &app)
{
	static detach_args r;

	auto cmd = app.add_subcommand("detach", "Detach a remote USB device")
		->callback(pack(cmd_detach, r))
		->require_option(1);

	auto opt_port = cmd->add_option("-p,--port", r.port, "Hub port number the device is plugged in")
		->check(CLI::Range(1, MAX_HUB_PORTS));

	cmd->add_option("-a,--all", "Detach all devices")
		->each( [&port = r.port] (const auto&) noexcept { port = vhci::port_all_closeonly; } ) // will not be called if argument is not passed
		->check(CLI::IsMember({"closeonly"}))
		->expected(0, 1)
		->excludes(opt_port);
}

void add_cmd_list(CLI::App &app)
{
	static list_args r;

	auto cmd = app.add_subcommand("list", "List exportable/persistent USB devices")
		->callback(pack(cmd_list, r))
		->require_option(1);

	cmd->add_option_group("Remote", "List exportable USB devices")
		->add_option("-r,--remote", r.remote, "List exportable devices on a remote")
		->required();

	cmd->add_option_group("Persistent", "List persistent USB devices")
		->add_flag("-s,--stashed,--persistent", r.persistent, "List persistent devices stashed by 'port --stash'");
}

void add_cmd_port(CLI::App &app)
{
	static port_args r;

	auto cmd = app.add_subcommand("port", "Show/stash imported USB devices")
		->callback(pack(cmd_port, r));

	cmd->add_flag("-s,--stash,--persistent", r.persistent,
		      "Devices listed by the command will be attached every time the driver is loaded (aka persistent devices)");
	
	cmd->add_option("number", r.ports, "Hub port number")
		->check(CLI::Range(1, MAX_HUB_PORTS))
		->expected(1, MAX_HUB_PORTS);
}

auto &msgtable_dll = L"resources"; // resource-only DLL that contains RT_MESSAGETABLE

auto& get_resource_module() noexcept
{
	static HModule mod(LoadLibraryEx(msgtable_dll, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR));
	return mod;
}

void init_spdlog()
{
	set_default_logger(spdlog::stderr_color_st("stderr"));
	spdlog::set_pattern("%^%l%$: %v");

	using fn = void(const std::string&);
	fn &f = spdlog::debug; // pick this overload
	libusbip::set_debug_output(f);
}

void init(CLI::App &app)
{
	app.option_defaults()->always_capture_default();
	app.set_version_flag("-V,--version", get_version());

	app.add_flag("-d,--debug", 
		[] (auto) { spdlog::set_level(spdlog::level::debug); }, "Debug output");

	app.add_option("-t,--tcp-port", global_args.tcp_port, "TCP/IP port number of USB/IP server")
		->check(CLI::Range(1024, USHRT_MAX));

	add_cmd_attach(app);
	add_cmd_detach(app);
	add_cmd_list(app);
	add_cmd_port(app);

	app.require_subcommand(1);
}

auto run(int argc, wchar_t *argv[])
{
	init_spdlog();

	if (!get_resource_module()) {
		auto err = GetLastError();
		spdlog::critical(L"can't load '{}.dll', error {:#x} {}", msgtable_dll, err, wformat_message(err));
		return EXIT_FAILURE;
	}

	InitWinSock2 ws2;
	if (!ws2) {
		spdlog::critical("can't initialize Windows Sockets 2, {}", get_last_error_msg());
		return EXIT_FAILURE;
	}

	CLI::App app("USB/IP client"); 
	init(app);

	try {                                                                                                              
		app.parse(argc, argv);
	} catch (CLI::ParseError &e) {
		return app.exit(e);
	}

	return EXIT_SUCCESS;
}

} // namespace


std::string usbip::get_last_error_msg(DWORD msg_id)
{
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
	try {
		return run(argc, argv);
	} catch (const std::exception &e) {
                std::println(stderr, "exception: {}", e.what());
                return EXIT_FAILURE;
	}
}
