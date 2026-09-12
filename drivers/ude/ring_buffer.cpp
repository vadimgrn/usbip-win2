/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "ring_buffer.h"
#include "trace.h"
#include "ring_buffer.tmh"

#include "driver.h"
#include <usbip/proto.h>

using namespace libdrv;

/*
* While the TransferBufferLength field itself is a 32-bit integer (ULONG),
* you cannot pass an arbitrary 4 GB buffer.
* The Microsoft USB driver stack (USBPORT.SYS / USBHUB3.SYS)
* enforces strict upper bounds based on the Host Controller Type:
* - USB 3.X (xHCI): Maximum size is 4 MB (4,194,304 bytes) per URB.
* - USB 2.0 High-Speed (EHCI): Maximum size is 4 MB.
* - USB 1.1 Full-Speed (OHCI): Maximum size drops to 256 KB.
* - USB 1.1 Full-Speed (UHCI): Maximum size is 4 MB.
*/
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::realloc(_Inout_ ring_buffer_data* &data, _In_ size_t bytes)
{
        enum : size_t {
                MB = 1024*1024, MAX_BYTES = 4*MB,
                struct_overhead = sizeof(*data)
        };

        if (!bytes) {
                free(data);
                return STATUS_SUCCESS;
        }

        if (bytes > MAX_BYTES) {
                Trace(TRACE_LEVEL_ERROR, "Requested data bytes %Iu exceeds %IuMB limit", bytes, MAX_BYTES/MB);
                return STATUS_INVALID_PARAMETER;
        }

        bytes = BYTES_TO_PAGES(bytes + struct_overhead)*PAGE_SIZE;
        auto capacity = bytes - struct_overhead;

        ring_buffer buf(data);
        auto buf_size = buf.size();

        if (buf_size > capacity) {
                Trace(TRACE_LEVEL_ERROR, "New capacity %Iu cannot hold current buffer size %Iu", capacity, buf_size);
                return STATUS_BUFFER_TOO_SMALL;
        }

        unique_ptr ptr(uninitialized, NonPagedPoolNx, bytes);
        if (!ptr) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate %Iu bytes", bytes);
                return STATUS_INSUFFICIENT_RESOURCES;
        }
        auto p = ptr.release<ring_buffer_data>();

        p->capacity = capacity;
        p->size = buf_size; 

        p->head = buf_size;
        p->tail = 0;

        p->buf = reinterpret_cast<char*>(p + 1);
        buf.peek(p->buf, buf_size);

        free(data);
        data = p;

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::free(_Inout_ ring_buffer_data* &p)
{
        unique_ptr{p};
        p = nullptr;
}

/*
 * Replace 64-bit modulo (% capacity) with conditional subtraction:
 * 64-bit division/modulo takes 20-40 CPU cycles on x64/ARM64.
 * Because to_write <= available() <= capacity, head + to_write is strictly < 2 * capacity.
 * Conditional subtraction executes in 1-2 cycles, avoiding division at DISPATCH_LEVEL.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
size_t usbip::ring_buffer::write(_In_reads_bytes_(len) const void *src, _In_ size_t len)
{
        if (!(len && src)) {
                return 0;
        }

        auto to_write = min(len, available());
        if (!to_write) {
                return 0;
        }

        auto first_chunk = min(to_write, m_data->capacity - m_data->head);
        if (first_chunk) {
                RtlCopyMemory(m_data->buf + m_data->head, src, first_chunk);
        }

        if (auto second_chunk = to_write - first_chunk) {
                RtlCopyMemory(m_data->buf, static_cast<const char*>(src) + first_chunk, second_chunk);
        }

        if (m_data->head += to_write; m_data->head >= m_data->capacity) {
                m_data->head -= m_data->capacity;
        }

        m_data->size += to_write;

        NT_ASSERT(size() <= capacity());
        return to_write;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
size_t usbip::ring_buffer::peek(_Out_writes_bytes_to_(len, return) void *dest, _In_ size_t len) const
{
        if (!(len && dest)) {
                return 0;
        }

        auto to_read = min(len, size());
        if (!to_read) {
                return 0;
        }

        auto first_chunk = min(to_read, m_data->capacity - m_data->tail);
        if (first_chunk) {
                RtlCopyMemory(dest, m_data->buf + m_data->tail, first_chunk);
        }

        if (auto second_chunk = to_read - first_chunk) {
                RtlCopyMemory(static_cast<char*>(dest) + first_chunk, m_data->buf, second_chunk);
        }

        return to_read;
}

/*
 * Replace 64-bit modulo (% capacity) with conditional subtraction.
 * @see write
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
size_t usbip::ring_buffer::skip(_In_ size_t len)
{
        if (len = min(len, size()); len) {
                if (m_data->tail += len; m_data->tail >= m_data->capacity) {
                        m_data->tail -= m_data->capacity;
                }
                m_data->size -= len;
        }

        NT_ASSERT(size() <= capacity());
        return len;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
size_t usbip::ring_buffer::read(_Out_writes_bytes_to_(len, return) void *dest, _In_ size_t len)
{
        auto n = peek(dest, len);
        return skip(n);
}

/*
 * The pointer inside the buffer is never returned,
 * it may be misaligned and will cause BSOD on ARM64.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool usbip::ring_buffer::peek_hdr(_Inout_ header &hdr) const
{
        return peek(hdr);
}
