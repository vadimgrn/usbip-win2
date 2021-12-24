#pragma once

#ifdef ALLOC_PRAGMA
  #define PAGEABLE __declspec(code_seg("PAGE"))
#else
  #define PAGEABLE
#endif
