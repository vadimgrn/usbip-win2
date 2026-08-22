/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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


template<typename HandleTraits>
class generic_handle
{
public:
        static auto None() NOEXCEPT { return HandleTraits::invalid(); }

        using type = decltype(None());
        using tag_type = HandleTraits;

        constexpr generic_handle() NOEXCEPT = default;
        constexpr explicit generic_handle(type h) NOEXCEPT : m_handle(h) {}

        ~generic_handle()
        {
                if (*this) {
                        close_handle(m_handle, tag_type{});
                }
        }

        generic_handle(const generic_handle&) = delete;
        generic_handle& operator=(const generic_handle&) = delete;

        constexpr generic_handle(generic_handle&& h) NOEXCEPT : m_handle(h.release()) {}

        auto& operator=(generic_handle&& h) NOEXCEPT
        {
                reset(h.release());
                return *this;
        }

        constexpr explicit operator bool() const NOEXCEPT { return m_handle != None(); }
        constexpr auto operator !() const NOEXCEPT { return m_handle == None(); }

        constexpr auto get() const NOEXCEPT { return m_handle; }

        template<typename T>
        constexpr auto get() const NOEXCEPT { return static_cast<T>(m_handle); }

        constexpr auto release() NOEXCEPT { return do_release(); }

        template<typename T>
        constexpr auto release() NOEXCEPT { return static_cast<T>(do_release()); }

        void reset(type h = None()) NOEXCEPT
        {
                if (m_handle != h) {
                        generic_handle(h).swap(*this);
                }

        }

        void close() NOEXCEPT { reset(); }

        constexpr void swap(generic_handle &h) NOEXCEPT
        {
                auto tmp = h.m_handle;
                h.m_handle = m_handle;
                m_handle = tmp;
        }

private:
        type m_handle = None();

        constexpr type do_release() NOEXCEPT
        {
                auto h = m_handle;
                m_handle = None();
                return h;
        }
};


template<typename HandleTraits>
constexpr void swap(generic_handle<HandleTraits> &a, generic_handle<HandleTraits> &b) NOEXCEPT
{
        a.swap(b);
}

} // namespace usbip
