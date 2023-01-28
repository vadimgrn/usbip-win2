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

bool fill(_Inout_ ioctl_plugin_hardware &r, 
        _In_ const std::string_view &host, 
        _In_ const std::string_view &service,
        _In_ const std::string_view &busid);

int attach(_In_ HANDLE dev, _Inout_ ioctl_plugin_hardware &r);
err_t detach(HANDLE dev, int port);

} // namespace usbip::vhci
