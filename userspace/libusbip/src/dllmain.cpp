/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE, DWORD reason_for_call, LPVOID /*reserved*/)
{
        switch (reason_for_call) {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
                break;
        }

        return true;
}
