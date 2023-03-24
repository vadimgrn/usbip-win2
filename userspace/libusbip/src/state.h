/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\dllspec.h"
#include <vector>

namespace usbip
{

struct device_location;

/*
 * @param success call GetLastError if false is returned
 * @return imported devices stashed by save_imported_devices() if success
 */
USBIP_API std::vector<device_location> load_imported_devices(_Out_ bool &success);

} // namespace usbip
