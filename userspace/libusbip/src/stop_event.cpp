/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "stop_event.h"

usbip::stop_event::stop_event(const std::stop_token &stoken) noexcept
{
        if (stoken.stop_requested()) {
                SetLastError(ERROR_CANCELLED);
                m_ok = false;
                return;
        }

        if (!stoken.stop_possible()) {
                return;
        }

        if (m_event.reset(CreateEvent(nullptr, true, false, nullptr)); m_event) {
                m_callback.emplace(stoken, callback_fn{ m_event.get() });
        } else {
                m_ok = false;
        }
}
