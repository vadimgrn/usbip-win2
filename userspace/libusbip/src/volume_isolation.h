/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "../volume_isolation.h"

#include <windows.h>
#include <expected>

namespace usbip
{

/**
 * Removes session-isolated drive letters associated with a USB port.
 *
 * @param port roothub port number (1-based)
 * @return call GetLastError() if false is returned
 * @note Called by vhci::detach() before hardware plugout to ensure
 *       session DosDevices links are removed while the DevNode hierarchy is still intact.
 */
bool cleanup_port_volumes(_In_ int port);

} // namespace usbip


namespace usbip::vhci
{

enum class port_storage { detached, maybe_storage, non_storage };
std::expected<port_storage, DWORD> get_port_storage(_In_ HANDLE dev, _In_ int port);

} // namespace usbip::vhci
