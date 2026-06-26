/*
 * Copyright (c) 2026, Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "ring_buffer.h"
#include "trace.h"
#include "ring_buffer.tmh"

#include "driver.h"
#include <usbip/proto.h>

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto usbip::realloc(_In_opt_ ring_buffer_data *data, _In_ size_t bytes) -> ring_buffer_data*
{
        enum { MAX_PAGES = 16 }; 
        static_assert(MAX_PAGES*PAGE_SIZE == 65536);

        if (!bytes) {
                free(data);
                return nullptr;
        }

        auto n = ROUND_TO_PAGES(bytes);
        if (n > MAX_PAGES) {
                Trace(TRACE_LEVEL_ERROR, "Upper limit is %d pages, %Iu is requested", MAX_PAGES, n);
                return nullptr;
        }

        bytes = n*PAGE_SIZE;
        auto capacity = bytes - offsetof(ring_buffer_data, buf);

        if (data && data->size > capacity) {
                Trace(TRACE_LEVEL_ERROR, "New capacity %Iu cannot hold current buffer size %Iu", capacity, data->size);
                return nullptr;
        }

        unique_ptr ptr(libdrv::uninitialized, NonPagedPoolNx, bytes);
        if (!ptr) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate %Iu bytes", bytes);
                return nullptr;
        }
        auto p = ptr.release<ring_buffer_data>();

        p->capacity = capacity;
        p->tail = 0;

        if (data) {
                ring_buffer buf(data);
                p->size = buf.peek(p->buf, buf.size());
                NT_ASSERT(p->size == buf.size());
                p->head = p->size;
                free(data);
        } else {
                p->size = 0;
                p->head = 0;
        }

        return p;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::free(_In_opt_ ring_buffer_data *p)
{
        unique_ptr{p}; 
}

size_t usbip::ring_buffer::write(_In_ const void *src, _In_ size_t len)
{
        if (!(src && len)) {
                return 0;
        }

        auto to_write = min(len, available());
        if (!to_write) {
                return 0;
        }

        auto first_chunk = min(to_write, m->capacity - m->head);
        RtlCopyMemory(m->buf + m->head, src, first_chunk);

        if (auto second_chunk = to_write - first_chunk) {
                RtlCopyMemory(m->buf, static_cast<const char*>(src) + first_chunk, second_chunk);
        }

        m->head = (m->head + to_write) % m->capacity;
        m->size += to_write;

        NT_ASSERT(m->size <= m->capacity);
        return to_write;
}

size_t usbip::ring_buffer::peek(_In_ void *dest, _In_ size_t len) const
{
        if (!(dest && len)) {
                return 0;
        }

        auto to_read = min(len, size());
        if (!to_read) {
                return 0;
        }

        auto first_chunk = min(to_read, m->capacity - m->tail);
        RtlCopyMemory(dest, m->buf + m->tail, first_chunk);

        if (auto second_chunk = to_read - first_chunk) {
                RtlCopyMemory(static_cast<char*>(dest) + first_chunk, m->buf, second_chunk);
        }

        return to_read;
}

size_t usbip::ring_buffer::skip(_In_ size_t len)
{
        len = min(len, size());

        m->tail = (m->tail + len) % m->capacity;
        m->size -= len;

        NT_ASSERT(m->size <= m->capacity);
        return len;
}

size_t usbip::ring_buffer::read(_In_ void *dest, _In_ size_t len)
{
        auto n = peek(dest, len);
        return skip(n);
}

/*
 * The pointer inside the buffer is never returned,
 * it may be misaligned and will crash on ARM64.
 * @return non-NULL pointer if a buffer is large enough to contain a header.
 */
bool usbip::ring_buffer::peek_hdr(_Inout_ header &hdr) const
{
        if (size() < sizeof(hdr)) {
                return false;
        }

        [[maybe_unused]] auto n = peek(&hdr, sizeof(hdr));
        NT_ASSERT(n == sizeof(hdr));

        return true;
}
