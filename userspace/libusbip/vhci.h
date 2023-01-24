#pragma once

#include "win_handle.h"

#include <usbip\vhci.h>

#include <string>
#include <vector>

namespace usbip::vhci
{

std::wstring get_path();
Handle open(const std::wstring &path = get_path());

std::vector<ioctl_get_imported_devices> get_imported_devs(HANDLE dev, bool &result);

bool attach(HANDLE dev, ioctl_plugin_hardware &r);
int detach(HANDLE dev, int port);

} // namespace usbip::vhci
