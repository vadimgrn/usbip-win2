/*
* Copyright (C) 2022 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include "pair.h"
#include <wdm.h>

namespace libdrv
{

struct uninitialized_t { explicit uninitialized_t() = default; };
inline constexpr uninitialized_t uninitialized;


template<ULONG PoolTag>
class unique_ptr
{
public:
        enum { pooltag = PoolTag };

        constexpr unique_ptr() = default;
        unique_ptr(_In_ void *ptr) : m_ptr(ptr) {}

        unique_ptr(_In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                m_ptr(ExAllocatePoolZero(PoolType, NumberOfBytes, pooltag)) {}

        unique_ptr(_In_ const uninitialized_t&, _In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                m_ptr(ExAllocatePoolUninitialized(PoolType, NumberOfBytes, pooltag)) {}

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
                auto ptr = m_ptr;
                m_ptr = nullptr;
                return ptr;
        }

        unique_ptr(_Inout_ unique_ptr&& p) : m_ptr(p.release()) {}

        auto& operator=(_Inout_ unique_ptr&& p)
        {
                reset(p.release());
                return *this;
        }

        void reset(_In_ void *ptr = nullptr)
        {
                if (m_ptr != ptr) {
                        unique_ptr(ptr).swap(*this);
                }
        }

        void swap(_Inout_ unique_ptr &p) { ::swap(m_ptr, p.m_ptr); }

private:
        void *m_ptr{};
};


template<ULONG PoolTag>
inline void swap(_Inout_ unique_ptr<PoolTag> &a, _Inout_ unique_ptr<PoolTag> &b)
{
        a.swap(b);
}

} // namespace libdrv
