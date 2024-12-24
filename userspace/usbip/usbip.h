/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <string>
#include <set>

#include <libusbip\remote.h>

namespace usbip
{

class UsbIds;
const UsbIds& get_ids();

std::string GetLastErrorMsg(unsigned long msg_id = ~0UL);

struct global_args
{
        std::string tcp_port = get_tcp_port();
};
inline struct global_args global_args;

using command_t = bool(void*);

struct attach_args
{
        // --remote
        std::string remote;
        std::string busid;
        bool terse{};

        // --stash
        bool stashed{};
};
command_t cmd_attach;

struct detach_args
{
        int port{};
};
command_t cmd_detach;

struct list_args
{
        // --remote
        std::string remote;

        // --stashed
        bool stashed{};
};
command_t cmd_list;

struct port_args
{
        std::set<int> ports;
        bool stash{};
};
command_t cmd_port;

} // namespace usbip
