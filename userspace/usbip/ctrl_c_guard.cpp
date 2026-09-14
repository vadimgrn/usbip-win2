/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "ctrl_c_guard.h"

#include <libusbip/vhci.h>

#include <atomic>
#include <cassert>

namespace
{

std::atomic<HANDLE> s_dev{};
std::atomic<std::stop_source*> s_ssrc{};

} // namespace


usbip::ctrl_c_guard::ctrl_c_guard() noexcept
{
        m_ssrc.emplace();
        assert(!s_dev.load() && !s_ssrc.load());
        s_ssrc.store(&*m_ssrc);

        [[maybe_unused]] auto ok = SetConsoleCtrlHandler(handler, true);
        assert(ok);
}

usbip::ctrl_c_guard::ctrl_c_guard(HANDLE dev) noexcept
{
        assert(!s_dev.load() && !s_ssrc.load());
        s_dev.store(dev);

        [[maybe_unused]] auto ok = SetConsoleCtrlHandler(handler, true);
        assert(ok);
}

usbip::ctrl_c_guard::ctrl_c_guard(std::stop_source &ssrc) noexcept
{
        assert(!s_dev.load() && !s_ssrc.load());
        s_ssrc.store(&ssrc);

        [[maybe_unused]] auto ok = SetConsoleCtrlHandler(handler, true);
        assert(ok);
}

usbip::ctrl_c_guard::~ctrl_c_guard() noexcept
{
        [[maybe_unused]] auto ok = SetConsoleCtrlHandler(handler, false);
        assert(ok);

        s_dev.store(HANDLE{});
        s_ssrc.store(nullptr);
}

std::stop_token usbip::ctrl_c_guard::token() const noexcept
{
        assert(s_ssrc.load());
        return s_ssrc.load()->get_token();
}

BOOL WINAPI usbip::ctrl_c_guard::handler(DWORD ctrl_type) noexcept
{
        if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {

                if (auto dev = s_dev.exchange(HANDLE{})) {
                        vhci::cancel_io(dev);
                        return true;
                }

                if (auto ssrc = s_ssrc.exchange(nullptr)) {
                        ssrc->request_stop();
                        return true;
                }
        }

        return false;
}
