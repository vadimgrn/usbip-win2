/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/generic_handle_ex.h>
#include <wdm.h>

namespace libdrv
{

template<ULONG PoolTag>
struct pool_ptr_traits
{
        static constexpr ULONG pooltag = PoolTag; // enum causes codeql error cpp/drivers/pool-tag-integral
        static void* invalid() { return nullptr; }
};


template<ULONG PoolTag>
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void close_handle(_In_ void *ptr, _In_ pool_ptr_traits<PoolTag> tag)
{
        ExFreePoolWithTag(ptr, tag.pooltag);
}

using usbip::swap;
using usbip::generic_handle;

struct uninitialized_t { explicit uninitialized_t() = default; };
inline constexpr uninitialized_t uninitialized;

template<ULONG PoolTag>
class unique_ptr_t : public generic_handle<pool_ptr_traits<PoolTag>>
{
        using base = generic_handle<pool_ptr_traits<PoolTag>>;
        using base::base;
public:
        static constexpr ULONG pooltag = PoolTag; // enum causes codeql error cpp/drivers/pool-tag-integral

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        unique_ptr_t(_In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                unique_ptr_t(ExAllocatePoolZero(PoolType, NumberOfBytes, pooltag)) {}

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        unique_ptr_t(_In_ const uninitialized_t&, _In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                unique_ptr_t(ExAllocatePoolUninitialized(PoolType, NumberOfBytes, pooltag)) {}

        unique_ptr_t(const unique_ptr_t&) = delete;
        unique_ptr_t& operator=(const unique_ptr_t&) = delete;

        unique_ptr_t(unique_ptr_t&&) = default;
        unique_ptr_t& operator=(unique_ptr_t&&) = default;

        using base::get;
        using base::release;

        template<typename T>
        constexpr T* get() const { return base::template get<T*>(); } // T* is used instead of auto to ensure the expected result

        template<typename T>
        constexpr T* release() { return base::template release<T*>(); } // same as above
};

} // namespace libdrv
