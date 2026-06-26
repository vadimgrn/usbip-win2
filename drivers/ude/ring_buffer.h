/*
 * Copyright (c) 2026, Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

namespace usbip
{

struct header;

struct ring_buffer_data
{
        size_t capacity; // allocated buffer size
        size_t size;

        size_t head; // write index
        size_t tail; // read index

        char buf[ANYSIZE_ARRAY];
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ring_buffer_data *realloc(_In_opt_ ring_buffer_data *data, _In_ size_t bytes);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void free(_In_opt_ ring_buffer_data *p);

/*
 * This class does not own data.
 */
class ring_buffer
{
public:
        constexpr ring_buffer() = default;
        constexpr ring_buffer(_In_opt_ ring_buffer_data *data) : m(data) { NT_ASSERT(m); }

        constexpr explicit operator bool() const { return m->buf; }
        constexpr auto operator !() const { return !m->buf; }

        auto capacity() const { return m->capacity; }
        auto size() const { return m->size; }
        size_t available() const { return m->capacity - m->size; }

        auto empty() const { return !m->size; }
        auto full() const { return m->size == m->capacity; }

        size_t write(_In_ const void *src, _In_ size_t len);

        size_t peek(_In_ void *dest, _In_ size_t len) const;
        size_t read(_In_ void *dest, _In_ size_t len);
        size_t skip(_In_ size_t len);

        bool peek_hdr(_Inout_ header &hdr) const;

private:
        ring_buffer_data *m{};
};

} // namespace usbip
