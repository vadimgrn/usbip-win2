/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

// Test libusbip API for C++17 compatibility.

#include <libusbip\format_message.h>
#include <libusbip\win_handle.h>
#include <libusbip\win_socket.h>
#include <libusbip\output.h>
#include <libusbip\remote.h>
#include <libusbip\vhci.h>
#include <libusbip\persistent.h>
#include <libusbip\src\setupapi.h>
#include <libusbip\src\hkey.h>

int main()
{
        using namespace usbip;

        // output.h
        libusbip::set_debug_output([] (std::string) {});
        [[maybe_unused]] auto &dbg = libusbip::get_debug_output();

        // format_message.h
        wformat_message(ERROR_INVALID_PARAMETER);
        wformat_message(nullptr, ERROR_INVALID_PARAMETER);
        wformat_message(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, ERROR_INVALID_PARAMETER, 0);
        format_message(ERROR_INVALID_PARAMETER);
        format_message(nullptr, ERROR_INVALID_PARAMETER);

        // win_handle.h
        Handle h;
        NullableHandle nh;
        HModule module;

        // win_socket.h
        InitWinSock2 ws2;
        Socket s;
        WSAEvent wevt;

        // setupapi.h & hkey.h
        hdevinfo devinfo;
        HInf inf;
        HspFileQ fq;
        HKey key;

        // remote.h
        [[maybe_unused]] auto port = get_tcp_port();
        auto s1 = connect("", "1234");
        auto s2 = connect("", "1234", CANCEL_BY_APC);
        if (s1) {
                enum_exportable_devices(s1.get(),
                        [] (int, const usb_device&) {},
                        [] (int, const usb_device&, int, const usb_interface&) {},
                        [] (int) {});
        }

        // vhci.h
        [[maybe_unused]] auto max_serial = get_device_serial_maxlen();
        [[maybe_unused]] auto valid_serial = validate_device_serial("test");
        [[maybe_unused]] auto gen_serial = generate_device_serial();

        auto dev = vhci::open();
        auto dev_ovl = vhci::open(true);

        [[maybe_unused]] auto imported = vhci::get_imported_devices(dev.get());

        vhci::attach_args args{};
        args.location.hostname = "localhost";
        args.location.service = "3240";
        args.location.busid = "1-1";
        args.serial = "test";
        args.recv_mode = receive_mode::zero_copy;
        args.once = true;

        [[maybe_unused]] auto attached_port = vhci::attach(dev.get(), args);
        [[maybe_unused]] auto stopped_cnt = vhci::stop_attach_attempts(dev.get(), &args.location);
        [[maybe_unused]] auto stopped_all = vhci::stop_attach_attempts(dev.get(), nullptr);
        [[maybe_unused]] auto detached = vhci::detach(dev.get(), 1);

        [[maybe_unused]] auto state_str = vhci::get_state_str(state::plugged);
        [[maybe_unused]] auto state_size = vhci::get_device_state_size();
        [[maybe_unused]] auto dev_state = vhci::get_device_state(nullptr, 0);
        [[maybe_unused]] auto read_state = vhci::read_device_state(dev.get());

        // persistent.h
        std::vector<persistent_device> pdevs{ args };
        [[maybe_unused]] auto set_pers = vhci::set_persistent(dev.get(), pdevs);
        [[maybe_unused]] auto get_pers = vhci::get_persistent(dev.get());
}
