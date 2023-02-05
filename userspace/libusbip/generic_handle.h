/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

namespace usbip
{

/*
 * Full specialization of this function must be defined for each used handle type.
 */
template<typename Handle, typename Tag>
void close_handle(Handle, Tag) noexcept;


template<typename Handle, typename Tag, auto NoneValue>
class generic_handle
{
public:
        using type = Handle;
        using tag_type = Tag;

        static constexpr auto None = NoneValue;

        constexpr generic_handle() = default;
        constexpr explicit generic_handle(type h) noexcept : m_handle(h) {}

        ~generic_handle() { do_close(); }

        generic_handle(const generic_handle&) = delete;
        generic_handle& operator=(const generic_handle&) = delete;

        generic_handle(generic_handle&& h) noexcept : m_handle(h.release()) {}

        auto& operator=(generic_handle&& h) noexcept
        {
                reset(h.release());
                return *this;
        }

        constexpr explicit operator bool() const noexcept { return m_handle != None; }
        constexpr auto operator !() const noexcept { return m_handle == None; }

        constexpr auto get() const noexcept { return m_handle; }

        auto release() noexcept
        {
                auto h = m_handle;
                m_handle = None;
                return h;
        }

        void reset(type h = None) noexcept
        {
                if (m_handle != h) {
                        do_close();
                        m_handle = h;
                }

        }

        void close() noexcept { reset(); }

        void swap(generic_handle &h) noexcept
        {
                auto tmp = h.m_handle;
                h.m_handle = m_handle;
                m_handle = tmp;
        }

private:
        type m_handle = None;

        void do_close() noexcept
        {
                if (*this) {
                        close_handle(m_handle, tag_type());
                }
        }
};

} // namespace usbip
