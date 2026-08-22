/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/generic_handle_ex.h>
#include <wdm.h>

namespace usbip
{

template<ULONG PoolTag>
struct pool_ptr_traits
{
        static constexpr ULONG pooltag = PoolTag; // enum causes codeql error cpp/drivers/pool-tag-integral
        static void* invalid() noexcept { return nullptr; }
};


template<ULONG PoolTag>
inline void close_handle(_In_ void *ptr, _In_ pool_ptr_traits<PoolTag> tag)
{
        ExFreePoolWithTag(ptr, tag.pooltag);
}

} // namespace usbip


namespace libdrv
{

using usbip::swap;
using usbip::generic_handle;

struct uninitialized_t { explicit uninitialized_t() = default; };
inline constexpr uninitialized_t uninitialized;

template<ULONG PoolTag>
class unique_ptr : public generic_handle<usbip::pool_ptr_traits<PoolTag>>
{
        using base = generic_handle<usbip::pool_ptr_traits<PoolTag>>;
        using base::base;
public:
        static constexpr ULONG pooltag = PoolTag; // enum causes codeql error cpp/drivers/pool-tag-integral

        unique_ptr(_In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                unique_ptr(ExAllocatePoolZero(PoolType, NumberOfBytes, pooltag)) {}

        unique_ptr(_In_ const uninitialized_t&, _In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                unique_ptr(ExAllocatePoolUninitialized(PoolType, NumberOfBytes, pooltag)) {}

        unique_ptr(const unique_ptr&) = delete;
        unique_ptr& operator=(const unique_ptr&) = delete;

        unique_ptr(unique_ptr&&) = default;
        unique_ptr& operator=(unique_ptr&&) = default;

        using base::get;
        using base::release;

        template<typename T>
        constexpr T* get() const { return base::template get<T*>(); } // T* is used instead of auto to ensure the expected result

        template<typename T>
        constexpr T* release() { return base::template release<T*>(); } // same as above
};

} // namespace libdrv
