#pragma once

#include "win_handle.h"

#include <usbip\vhci.h>

#include <string>
#include <vector>

/*
 * Strings encoding is UTF8. 
 */

namespace usbip::vhci
{

std::wstring get_path();
Handle open(const std::wstring &path = get_path());

std::vector<ioctl_get_imported_devices> get_imported_devs(HANDLE dev, bool &result);

struct attach_args
{
        std::string hostname;
        std::string service;
        std::string busid;
};
int attach(_In_ HANDLE dev, _In_ const attach_args &args);

err_t detach(HANDLE dev, int port);

} // namespace usbip::vhci
