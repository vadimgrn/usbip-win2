/*
 * Copyright (c) 2026, Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntdef.h>

namespace usbip
{

struct header;

struct ring_buffer_data
{
        size_t capacity;
        size_t size;

        size_t head; // write index
        size_t tail; // read index

        char buf[ANYSIZE_ARRAY]; // buf[capacity]
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS realloc(_Inout_ ring_buffer_data* &data, _In_ size_t bytes);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void free(_Inout_ ring_buffer_data* &p);

/*
 * This class does not own the data.
 */
class ring_buffer
{
public:
        constexpr ring_buffer(_In_ ring_buffer_data* &data) : m_data(data) {}

        ring_buffer(_In_ const ring_buffer&) = delete;
        ring_buffer& operator =(_In_ const ring_buffer&) = delete;

        constexpr explicit operator bool() const { return m_data; }
        constexpr auto operator !() const { return !m_data; }

        auto realloc(_In_ size_t bytes) { return usbip::realloc(m_data, bytes); }

        auto capacity() const { return m_data ? m_data->capacity : 0; }
        auto size() const { return m_data ? m_data->size : 0; }
        size_t available() const { return capacity() - size(); }

        auto empty() const { return !size(); }
        auto full() const { return size() == capacity(); }

        size_t write(_In_ const void *src, _In_ size_t len);

        size_t peek(_In_ void *dest, _In_ size_t len) const;
        size_t read(_In_ void *dest, _In_ size_t len);
        size_t skip(_In_ size_t len);

        bool peek_hdr(_Inout_ header &hdr) const;

private:
        ring_buffer_data* &m_data;
};

} // namespace usbip
