/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#ifdef USBIP_EXPORTS
  #define USBIP_API __declspec(dllexport)
#else
  #define USBIP_API __declspec(dllimport)
#endif
