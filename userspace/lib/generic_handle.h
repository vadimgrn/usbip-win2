#pragma once

namespace usbip
{

/*
 * Full specialization of this function must be defined for each used handle type.
 */
template<typename T>
void close_handle(T) noexcept;


template<typename T, auto NullValue>
class GenericHandle
{
public:
        using Type = T;
        static constexpr auto Null = NullValue;

        explicit GenericHandle(Type h = Null) noexcept : m_handle(h) {}
        ~GenericHandle() { close(); }

        GenericHandle(const GenericHandle&) = delete;
        GenericHandle& operator=(const GenericHandle&) = delete;

        GenericHandle(GenericHandle&& h) noexcept : m_handle(h.release()) {}

        GenericHandle& operator=(GenericHandle&& h) noexcept
        {
                reset(h.release());
                return *this;
        }

        explicit operator bool() const noexcept { return m_handle != Null; }
        bool operator !() const noexcept { return m_handle == Null; }

        auto get() const noexcept { return m_handle; }

        auto release() noexcept
        {
                auto h = m_handle;
                m_handle = Null;
                return h;
        }

        void reset(Type h = Null) noexcept
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

        void swap(GenericHandle& h) noexcept
        {
                auto tmp = h.m_handle;
                h.m_handle = m_handle;
                m_handle = tmp;
        }

private:
        Type m_handle = Null;
};

} // namespace usbip
