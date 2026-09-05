/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/vhci.h>
#include <libusbip/remote.h>

#include <set>
#include <string>

namespace usbip
{

class UsbIds;
const UsbIds& get_ids();

enum class receive_mode;
const char *to_string(_In_ receive_mode mode) noexcept;

std::string get_last_error_msg(DWORD msg_id = GetLastError());

struct global_args
{
        std::string tcp_port = get_tcp_port();
};
inline global_args global_args;

struct attach_args
{
        // --remote
        std::string remote;
        std::string busid;
        std::string serial;
        bool terse{};
        bool stop{};
        bool once{};
        receive_mode recv_mode = receive_mode::zero_copy;

        // --persistent,--stashed
        bool persistent{};

        // --stop-all
        bool stop_all{};
};
bool cmd_attach(const attach_args &args);

struct detach_args
{
        int port = vhci::port_all;
};
bool cmd_detach(const detach_args &args);

struct list_args
{
        // --remote
        std::string remote;

        // --persistent,--stashed
        bool persistent{};
};
bool cmd_list(const list_args &args);

struct port_args
{
        std::set<int> ports;
        bool persistent{};
};
bool cmd_port(const port_args &args);

} // namespace usbip
