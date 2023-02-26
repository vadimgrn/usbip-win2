/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include "pair.h"
#include <wdm.h>

namespace libdrv
{

template<ULONG PoolTag>
class unique_ptr
{
public:
        enum { pooltag = PoolTag };

        constexpr unique_ptr() = default;
        unique_ptr(void *ptr) : m_ptr(ptr) {}

        unique_ptr(_In_ POOL_FLAGS Flags, _In_ SIZE_T NumberOfBytes) :
                m_ptr(ExAllocatePool2(Flags, NumberOfBytes, pooltag)) {}

        ~unique_ptr() 
        {
                if (m_ptr) {
                        ExFreePoolWithTag(m_ptr, pooltag);
                }
        }

        explicit operator bool() const { return m_ptr; }
        auto operator!() const { return !m_ptr; }

        auto get() const { return m_ptr; }

        template<typename T>
        auto get() const { return static_cast<T*>(m_ptr); }

        auto release()
        {
                auto p = m_ptr;
                m_ptr = nullptr;
                return p;
        }

        unique_ptr(unique_ptr&& p) : m_ptr(p.release()) {}

        auto& operator=(unique_ptr&& p)
        {
                reset(p.release());
                return *this;
        }

        void reset(void *ptr = nullptr)
        {
                if (m_ptr != ptr) {
                        unique_ptr(ptr).swap(*this);
                }
        }

        void swap(unique_ptr &p) { ::swap(m_ptr, p.m_ptr); }

private:
        void *m_ptr{};
};


template<ULONG PoolTag>
inline void swap(unique_ptr<PoolTag> &a, unique_ptr<PoolTag> &b)
{
        a.swap(b);
}

} // namespace libdrv
