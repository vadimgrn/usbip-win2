/*
* Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>

namespace libdrv
{

enum class memory { nonpaged, paged, stack = paged };

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
MDL *tail(_In_opt_ MDL *mdl);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
size_t size(_In_opt_ const MDL *mdl);

class Mdl
{
public:
        constexpr Mdl() = default;
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        Mdl(_In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        Mdl(_In_ MDL *SourceMdl, _In_ ULONG Offset, _In_ ULONG Length);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~Mdl() { reset(); }

        Mdl(const Mdl&) = delete;
        Mdl& operator =(const Mdl&) = delete;

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        Mdl(_Inout_ Mdl&& m);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        Mdl& operator =(_Inout_ Mdl&& m);

        constexpr explicit operator bool() const { return m_mdl; }
        constexpr auto operator !() const { return !m_mdl; }

        constexpr bool operator ==(decltype(nullptr)) const { return m_mdl == nullptr; }
        constexpr bool operator !=(decltype(nullptr)) const { return m_mdl != nullptr; }

        constexpr auto get() const { return m_mdl; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto vaddr() const { return m_mdl ? MmGetMdlVirtualAddress(m_mdl) : nullptr; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto size() const { return m_mdl ? MmGetMdlByteCount(m_mdl) : 0; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void *sysaddr(_In_ ULONG Priority = NormalPagePriority | MdlMappingNoExecute);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        NTSTATUS prepare_nonpaged();

        _IRQL_requires_same_
        _IRQL_requires_max_(APC_LEVEL)
        NTSTATUS prepare_paged(_In_ LOCK_OPERATION Operation);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void reset() { reset(nullptr, false); }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto next() const { return m_mdl ? m_mdl->Next : nullptr; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void next(_In_opt_ MDL *m);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        auto& next(_Inout_ Mdl &m) { next(m.get()); return m; }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void swap(_Inout_ Mdl &other);

private:
        MDL *m_mdl{};
        bool m_mapped{};

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        bool locked() const { return m_mdl && (m_mdl->MdlFlags & MDL_PAGES_LOCKED); }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        bool nonpaged() const { return m_mdl && (m_mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL); }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        bool partial() const { return m_mdl && (m_mdl->MdlFlags & MDL_PARTIAL); }

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        bool mapped() const { return m_mdl && (m_mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA); }

        _IRQL_requires_same_
        _IRQL_requires_max_(APC_LEVEL)
        NTSTATUS lock(_In_ LOCK_OPERATION Operation);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void unprepare();

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        MDL *release();

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void reset(_In_opt_ MDL *mdl, _In_ bool mapped);
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void swap(_Inout_ Mdl &a, _Inout_ Mdl &b)
{
        a.swap(b);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto tail(_In_ const Mdl &mdl) { return tail(mdl.get()); }

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto size(_In_ const Mdl &mdl) { return size(mdl.get()); }

} // namespace libdrv
