/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <string>
#include <set>

#include <usbip\consts.h>

class UsbIds;

namespace usbip
{

const UsbIds& get_ids();

struct global_args
{
        unsigned short tcp_port = usbip_port;
};
inline struct global_args global_args;

struct attach_args
{
        std::string remote;
        std::string busid;
        std::string serial;
        bool terse{};
};
int cmd_attach(attach_args &r);

struct detach_args
{
        int port;
};
int cmd_detach(detach_args &r);

struct list_args
{
        std::string remote;
};
int cmd_list(list_args &r);

struct port_args
{
        std::set<int> ports;
};
int cmd_port(port_args &r);

} // namespace usbip
