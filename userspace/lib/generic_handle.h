#pragma once

namespace usbip
{

/*
 * Full specialization of this function must be defined for each used handle type.
 */
template<typename T>
void close_handle(T) noexcept;


template<typename T, auto NullValue>
class generic_handle
{
public:
        using type = T;
        static constexpr auto null = NullValue;

        explicit generic_handle(type h = null) noexcept : m_handle(h) {}
        ~generic_handle() { close(); }

        generic_handle(const generic_handle&) = delete;
        generic_handle& operator=(const generic_handle&) = delete;

        generic_handle(generic_handle&& h) noexcept : m_handle(h.release()) {}

        generic_handle& operator=(generic_handle&& h) noexcept
        {
                reset(h.release());
                return *this;
        }

        explicit operator bool() const noexcept { return m_handle != null; }
        bool operator !() const noexcept { return m_handle == null; }

        auto get() const noexcept { return m_handle; }

        auto release() noexcept
        {
                auto h = m_handle;
                m_handle = null;
                return h;
        }

        void reset(type h = null) noexcept
        {
                if (m_handle == h) {
                        return;
                }

                if (*this) {
                        close_handle(m_handle);
                }

                m_handle = h;
        }

        void close() noexcept { reset(); }

        void swap(generic_handle& h) noexcept
        {
                auto tmp = h.m_handle;
                h.m_handle = m_handle;
                m_handle = tmp;
        }

private:
        type m_handle = null;
};

} // namespace usbip
