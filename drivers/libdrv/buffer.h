/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include "unique_ptr.h"

namespace libdrv
{

const ULONG pooltag = 'VRDL';

class buffer
{
public:
        constexpr buffer() = default;

        buffer(_In_ POOL_FLAGS Flags, _In_ SIZE_T NumberOfBytes, _In_ ULONG Tag = pooltag) :
                m_ptr(ExAllocatePool2(Flags, NumberOfBytes, Tag), Tag),
                m_size(NumberOfBytes) {}

        buffer(const buffer&) = delete;
        buffer& operator=(const buffer&) = delete;

        buffer(buffer&&) = default;
        buffer& operator=(buffer&&) = default;

        explicit operator bool() const { return static_cast<bool>(m_ptr); }
        auto operator!() const { return !m_ptr; }

        auto size() const { return m_ptr ? m_size : 0; }
        auto tag() const { return m_ptr.tag(); }
        auto get() const { return m_ptr.get(); }

        template<typename T>
        auto get() const { return m_ptr.get<T>(); }

        void swap(buffer &b)
        {
                m_ptr.swap(b.m_ptr);
                ::swap(m_size, b.m_size);
        }

private:
        unique_ptr m_ptr;
        SIZE_T m_size{};
};


inline void swap(buffer &a, buffer &b)
{
        a.swap(b);
}

} // namespace libdrv
