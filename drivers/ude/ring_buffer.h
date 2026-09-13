/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

namespace usbip
{

struct header;

struct ring_buffer_data
{
        size_t capacity;
        size_t size;

        size_t head; // write index
        size_t tail; // read index

        char *buf; // points to data
//      char data[capacity]; // @see realloc
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
        constexpr ring_buffer() = default;
        constexpr ring_buffer(_In_opt_ ring_buffer_data *data) : m_data(data) {}

        constexpr explicit operator bool() const { return m_data; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto capacity() const { return m_data ? m_data->capacity : 0; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto size() const { return m_data ? m_data->size : 0; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        size_t available() const { return capacity() - size(); }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto empty() const { return !size(); }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto full() const { return size() == capacity(); }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        size_t write(_In_reads_bytes_(len) const void *src, _In_ size_t len);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        size_t peek(_Out_writes_bytes_to_(len, return) void *dest, _In_ size_t len) const;

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        size_t read(_Out_writes_bytes_to_(len, return) void *dest, _In_ size_t len);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        size_t skip(_In_ size_t len);

        template <typename T>
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        bool peek(_Out_ T &dest) const
        {
                return size() >= sizeof(T) && peek(&dest, sizeof(T)) == sizeof(T);
        }

        template <typename T>
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        bool read(_Out_ T &dest)
        {
                return size() >= sizeof(T) && read(&dest, sizeof(T)) == sizeof(T);
        }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        bool peek_hdr(_Inout_ header &hdr) const;

private:
        ring_buffer_data *m_data{};
};

} // namespace usbip
