/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "threadpool.h"

namespace
{
        TP_CALLBACK_ENVIRON g_cbe;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason_for_call, LPVOID /*reserved*/)
{
        switch (reason_for_call) {
        case DLL_PROCESS_DETACH:
                DestroyThreadpoolEnvironment(&g_cbe);
                break;
        case DLL_PROCESS_ATTACH:
                InitializeThreadpoolEnvironment(&g_cbe);
                SetThreadpoolCallbackLibrary(&g_cbe, hModule);
                break;
        }

        return true;
}

PTP_CALLBACK_ENVIRON usbip::get_threadpool_environ() noexcept
{
        return &g_cbe;
}
