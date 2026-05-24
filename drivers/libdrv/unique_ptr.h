/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "utils.h"
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
        constexpr unique_ptr(_In_ void *ptr) : m_ptr(ptr) {}

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

        constexpr explicit operator bool() const { return m_ptr; }
        constexpr auto operator!() const { return !m_ptr; }

        constexpr auto get() const { return m_ptr; }

        template<typename T>
        constexpr auto get() const { return static_cast<T*>(m_ptr); }

        template<typename T = void>
        constexpr auto release()
        {
                auto ptr = get<T>();
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

        constexpr void swap(_Inout_ unique_ptr &p) { ::swap(m_ptr, p.m_ptr); }

private:
        void *m_ptr{};
};


template<ULONG PoolTag>
constexpr void swap(_Inout_ unique_ptr<PoolTag> &a, _Inout_ unique_ptr<PoolTag> &b)
{
        a.swap(b);
}

} // namespace libdrv
