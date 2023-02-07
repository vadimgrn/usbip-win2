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

std::vector<imported_device> get_imported_devices(HANDLE dev, bool &result);

bool fill(_Inout_ ioctl_plugin_hardware &r, 
        _In_ std::string_view host, 
        _In_ std::string_view service,
        _In_ std::string_view busid);

int attach(_In_ HANDLE dev, _Inout_ ioctl_plugin_hardware &r);
bool detach(HANDLE dev, int port);

} // namespace usbip::vhci
