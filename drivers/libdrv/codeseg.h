#pragma once

#include <wdm.h> // defines ALLOC_PRAGMA, ALLOC_DATA_PRAGMA

#ifdef ALLOC_PRAGMA
  #define PAGED   __declspec(code_seg("PAGE"))
  #define CS_INIT __declspec(code_seg("INIT"))
#else
  #define PAGED
  #define CS_INIT
#endif
