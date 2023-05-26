/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

namespace usbip
{

enum class memory { nonpaged, paged, stack = paged };

MDL *tail(_In_opt_ MDL *mdl);
size_t size(_In_opt_ const MDL *mdl);

class Mdl
{
public:
        Mdl(_In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length);
        Mdl(_In_ MDL *SourceMdl, _In_ ULONG Offset, _In_ ULONG Length);

        ~Mdl() { reset(); }

        Mdl(const Mdl&) = delete;
        Mdl& operator =(const Mdl&) = delete;

        Mdl(Mdl&& m) : m_mdl(m.release()) {}
        Mdl& operator =(Mdl&& m);

        explicit operator bool() const { return m_mdl; }
        auto operator !() const { return !m_mdl; }

        auto get() const { return m_mdl; }

        auto vaddr() const { return m_mdl ? MmGetMdlVirtualAddress(m_mdl) : nullptr; }
        auto size() const { return m_mdl ? MmGetMdlByteCount(m_mdl) : 0; }

        void *sysaddr(_In_ ULONG Priority = NormalPagePriority | MdlMappingNoExecute);

        NTSTATUS prepare_nonpaged();
        NTSTATUS prepare_paged(_In_ LOCK_OPERATION Operation);

        void reset() { reset(nullptr); }

        auto next() const { return m_mdl ? m_mdl->Next : nullptr; }
        void next(_In_opt_ MDL *m);
        auto& next(_Inout_ Mdl &m) { next(m.get()); return m; }

private:
        MDL *m_mdl{};

        bool locked() const { return m_mdl->MdlFlags & MDL_PAGES_LOCKED; }
        bool nonpaged() const { return m_mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL; }
        bool partial() const { return m_mdl->MdlFlags & MDL_PARTIAL; } // MDL_PARTIAL_HAS_BEEN_MAPPED

        NTSTATUS lock(_In_ LOCK_OPERATION Operation);
        void unprepare();

        MDL *release();
        void reset(_In_opt_ MDL *mdl);
};

inline auto tail(_In_ const Mdl &mdl) { return tail(mdl.get()); }
inline auto size(_In_ const Mdl &mdl) { return size(mdl.get()); }

} // namespace usbip
