/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "cancel_wait_guard.h"
#include <cassert>

usbip::cancel_wait_guard::cancel_wait_guard(
        _In_opt_ HANDLE cancel_event, _In_ WAITORTIMERCALLBACK cb, _In_ void *ctx) noexcept :
        m_ok(!cancel_event ||
              RegisterWaitForSingleObject(&m_handle, cancel_event, cb, ctx, INFINITE, WT_EXECUTEONLYONCE)) {}

/*
 * If the pending I/O completes just before the cancellation thread pool callback executes,
 * CancelIoEx will return FALSE with GetLastError() == ERROR_NOT_FOUND.
 */
void CALLBACK usbip::cancel_wait_guard::handle_cb(_In_ void *ctx, BOOLEAN) noexcept
{
        CancelIoEx(static_cast<HANDLE>(ctx), nullptr);
}

/*
 * shutdown: best-effort wake up for blocking recv/send, can harmlessly fail
 * (e.g. WSAENOTCONN) if already disconnected or reset by peer.
 */
void CALLBACK usbip::cancel_wait_guard::socket_cb(_In_ void *ctx, BOOLEAN) noexcept
{
        shutdown(reinterpret_cast<SOCKET>(ctx), SD_BOTH);
}

void usbip::cancel_wait_guard::reset() noexcept
{
        if (auto &h = m_handle) {
                [[maybe_unused]] auto ok = UnregisterWaitEx(h, INVALID_HANDLE_VALUE);
                assert(ok);
                h = nullptr;
        }
}
