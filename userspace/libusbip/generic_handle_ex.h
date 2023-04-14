/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#ifdef _KERNEL_MODE
  #define NOEXCEPT
#else
  #define NOEXCEPT noexcept
#endif

namespace usbip
{

/*
 * Full specialization of this function must be defined for each used handle type.
 */
template<typename Handle, typename Tag>
void close_handle(Handle, Tag) NOEXCEPT;


template<typename Handle, typename Tag, auto NoneValue>
class generic_handle
{
public:
        using type = Handle;
        using tag_type = Tag;

        static constexpr auto None = NoneValue;

        constexpr generic_handle() NOEXCEPT = default;
        constexpr explicit generic_handle(type h) NOEXCEPT : m_handle(h) {}

        ~generic_handle() 
        {
                if (*this) {
                        close_handle(m_handle, tag_type());
                }
        }

        generic_handle(const generic_handle&) = delete;
        generic_handle& operator=(const generic_handle&) = delete;

        generic_handle(generic_handle&& h) NOEXCEPT : m_handle(h.release()) {}

        auto& operator=(generic_handle&& h) NOEXCEPT
        {
                reset(h.release());
                return *this;
        }

        constexpr explicit operator bool() const NOEXCEPT { return m_handle != None; }
        constexpr auto operator !() const NOEXCEPT { return m_handle == None; }

        constexpr auto get() const NOEXCEPT { return m_handle; }

        template<typename T>
        constexpr auto get() const NOEXCEPT { return static_cast<T>(m_handle); }

        auto release() NOEXCEPT
        {
                auto h = m_handle;
                m_handle = None;
                return h;
        }

        void reset(type h = None) NOEXCEPT
        {
                if (m_handle != h) {
                        generic_handle(h).swap(*this);
                }

        }

        void close() NOEXCEPT { reset(); }

        void swap(generic_handle &h) NOEXCEPT
        {
                auto tmp = h.m_handle;
                h.m_handle = m_handle;
                m_handle = tmp;
        }

private:
        type m_handle = None;
};


template<typename Handle, typename Tag, auto NoneValue>
inline void swap(generic_handle<Handle, Tag, NoneValue> &a, 
                 generic_handle<Handle, Tag, NoneValue> &b) NOEXCEPT
{
        a.swap(b);
}

} // namespace usbip
