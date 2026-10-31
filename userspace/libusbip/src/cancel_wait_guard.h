/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <winsock2.h>

namespace usbip
{

/*
 * Monitors cancel_event via the thread pool to abort blocking synchronous I/O.
 * When cancel_event is signaled:
 * - for SOCKET: calls shutdown(s, SD_BOTH) to interrupt ongoing recv()/send().
 * - for HANDLE: calls CancelIoEx(dev, nullptr) to cancel pending synchronous I/O.
 *
 * Destructor/reset() uses UnregisterWaitEx(..., INVALID_HANDLE_VALUE) to synchronously
 * wait for any executing callback to complete before the guarded object is closed.
 *
 * cancel_event must be a manual-reset event: the thread pool wait consumes the signal
 * of an auto-reset event, so subsequent check_cancelled() calls would not detect it.
 *
 * Cancellation of a SOCKET is destructive: once shut down, the socket can't be used
 * for further I/O. Call reset() before the final check_cancelled() to guarantee that
 * the socket was not shut down if check_cancelled() reports no cancellation.
 */
class cancel_wait_guard
{
public:
        cancel_wait_guard() noexcept = default;

        cancel_wait_guard(_In_ HANDLE dev, _In_opt_ HANDLE cancel_event = HANDLE{}) noexcept :
                cancel_wait_guard(cancel_event, handle_cb, dev) {}

        cancel_wait_guard(_In_ SOCKET s, _In_opt_ HANDLE cancel_event = HANDLE{}) noexcept :
                cancel_wait_guard(cancel_event, socket_cb, reinterpret_cast<void*>(s)) {}

        ~cancel_wait_guard() { reset(); }

        cancel_wait_guard(const cancel_wait_guard&) = delete;
        cancel_wait_guard& operator=(const cancel_wait_guard&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return m_ok; }
        void reset() noexcept;

private:
        HANDLE m_handle{};
        bool m_ok = true;

        cancel_wait_guard(_In_opt_ HANDLE cancel_event, _In_ WAITORTIMERCALLBACK cb, _In_ void *ctx) noexcept;

        static void CALLBACK handle_cb(_In_ void *ctx, BOOLEAN) noexcept;
        static void CALLBACK socket_cb(_In_ void *ctx, BOOLEAN) noexcept;
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

} // namespace usbip
