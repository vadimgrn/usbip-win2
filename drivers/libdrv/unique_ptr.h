/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/generic_handle_ex.h>
#include <wdm.h>

namespace usbip
{

template<ULONG PoolTag>
struct pool_ptr_tag
{
        enum { value = PoolTag };
};

template<ULONG PoolTag>
inline void close_handle(_In_ void *ptr, _In_ pool_ptr_tag<PoolTag> tag)
{
        ExFreePoolWithTag(ptr, tag.value);
}

} // namespace usbip


namespace libdrv
{

using usbip::swap;

struct uninitialized_t { explicit uninitialized_t() = default; };
inline constexpr uninitialized_t uninitialized;

template<ULONG PoolTag>
class unique_ptr : public usbip::generic_handle<void*, usbip::pool_ptr_tag<PoolTag>, nullptr>
{
        using base = usbip::generic_handle<void*, usbip::pool_ptr_tag<PoolTag>, nullptr>;
        using base::base;
public:
        enum { pooltag = PoolTag };

        unique_ptr(_In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                unique_ptr(ExAllocatePoolZero(PoolType, NumberOfBytes, pooltag)) {}

        unique_ptr(_In_ const uninitialized_t&, _In_ POOL_TYPE PoolType, _In_ SIZE_T NumberOfBytes) :
                unique_ptr(ExAllocatePoolUninitialized(PoolType, NumberOfBytes, pooltag)) {}

        using base::get;
        using base::release;

        template<typename T>
        constexpr T* get() const { return base::template get<T*>(); } // T* is used instead of auto to ensure the expected result

        template<typename T>
        constexpr T* release() { return base::template release<T*>(); } // same as above
};

} // namespace libdrv
