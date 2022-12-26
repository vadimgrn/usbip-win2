/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include "unique_ptr.h"

namespace libdrv
{

template<ULONG PoolTag>
class buffer
{
public:
        enum { pooltag = PoolTag };

        constexpr buffer() = default;

        buffer(_In_ POOL_FLAGS Flags, _In_ SIZE_T NumberOfBytes) :
                m_ptr(ExAllocatePool2(Flags, NumberOfBytes, m_ptr.pooltag)),
                m_size(NumberOfBytes) {}

        buffer(buffer&&) = default;
        buffer& operator=(buffer&&) = default;

        explicit operator bool() const { return static_cast<bool>(m_ptr); }
        auto operator!() const { return !m_ptr; }

        auto size() const { return m_ptr ? m_size : 0; }
        auto get() const { return m_ptr.get(); }

        template<typename T>
        auto get() const { return m_ptr.get<T>(); }

        void swap(buffer &b)
        {
                m_ptr.swap(b.m_ptr);
                ::swap(m_size, b.m_size);
        }

private:
        unique_ptr<pooltag> m_ptr;
        SIZE_T m_size{};
};


template<ULONG PoolTag>
inline void swap(buffer<PoolTag> &a, buffer<PoolTag> &b)
{
        a.swap(b);
}

} // namespace libdrv
