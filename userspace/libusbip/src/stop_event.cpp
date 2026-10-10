/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "stop_event.h"
#include "../remote.h"

usbip::stop_event::stop_event(const std::stop_token &stoken) noexcept
{
        if (!stoken.stop_possible()) {
                return;
        }

        if (check_cancelled(stoken)) {
                m_ok = false;
        } else if (m_event = create_cancel_event(); !m_event) {
                m_ok = false;
        } else {
                m_callback.emplace(stoken, callback_fn{ m_event.get() });
        }
}
