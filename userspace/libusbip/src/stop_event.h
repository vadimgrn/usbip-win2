/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "../win_handle.h"

#include <optional>
#include <stop_token>

namespace usbip
{

/*
 * Bridges a C++20 std::stop_token to a Win32 manual-reset event HANDLE.
 *
 * If stop was already requested, sets LastError to ERROR_CANCELLED and
 * operator bool() returns false without creating an event.
 *
 * m_event must be declared before m_callback so that m_callback is destroyed first
 * (in reverse order of declaration) to unregister from the stop_token before the
 * event handle is closed by the NullableHandle destructor.
 */
class stop_event
{
public:
        explicit stop_event(const std::stop_token &stoken) noexcept;

        stop_event(const stop_event&) = delete;
        stop_event& operator=(const stop_event&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return m_ok; }
        [[nodiscard]] _Ret_maybenull_ auto get() const noexcept { return m_event.get(); }

private:
        struct callback_fn
        {
                void operator() () const noexcept
                {
                        [[maybe_unused]] auto ok = SetEvent(event);
                        assert(ok);
                }

                HANDLE event{};
        };

        NullableHandle m_event;
        std::optional<std::stop_callback<callback_fn>> m_callback;
        bool m_ok = true;
};

/*
 * Checks if a handle is in the signaled state.
 */
[[nodiscard]] inline auto is_signaled(_In_opt_ HANDLE h) noexcept
{
        return h && WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
}

/*
 * Checks if cancel_event is signaled.
 * If signaled, sets LastError to ERROR_CANCELLED.
 */
[[nodiscard]] inline DWORD check_cancelled(_In_opt_ HANDLE cancel_event) noexcept
{
        auto err = ERROR_SUCCESS;
        if (is_signaled(cancel_event)) {
                err = ERROR_CANCELLED;
                SetLastError(err);
        }
        return err;
}

/*
 * Checks if stop has been requested on stoken.
 * If requested, sets LastError to ERROR_CANCELLED.
 */
[[nodiscard]] inline DWORD check_cancelled(const std::stop_token &stoken) noexcept
{
        auto err = ERROR_SUCCESS;
        if (stoken.stop_requested()) {
                err = ERROR_CANCELLED;
                SetLastError(err);
        }
        return err;
}

} // namespace usbip
