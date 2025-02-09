/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h> // defines ALLOC_PRAGMA, ALLOC_DATA_PRAGMA

#ifdef ALLOC_PRAGMA
  #define PAGED   __declspec(code_seg("PAGE"))
  #define CS_INIT __declspec(code_seg("INIT"))
#else
  #define PAGED
  #define CS_INIT
#endif

inline auto ptr04x(const void *ptr) // use format "%04x"
{
        auto n = reinterpret_cast<uintptr_t>(ptr);
        return static_cast<UINT32>(n);
}
