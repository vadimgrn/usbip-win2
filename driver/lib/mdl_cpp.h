#pragma once

#include <wdm.h>

namespace usbip
{

enum class memory_pool { nonpaged, paged, stack = paged };

/*
 * Usage:
 * a) create an instance
 * b) prepare()
 * c) sysaddr()
 * d) unprepare()
 */
class Mdl
{
        enum { DEF_ACCESS_MODE = KernelMode };
public:
        Mdl(_In_ memory_pool pool, _In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length);
        ~Mdl();

        explicit operator bool() const { return m_mdl; }
        auto operator !() const { return !m_mdl; }

        auto get() const { return m_mdl; }

        auto addr() const { return m_mdl ? MmGetMdlVirtualAddress(m_mdl) : nullptr; }
        auto offset() const { return m_mdl ? MmGetMdlByteOffset(m_mdl) : 0; }
        auto size() const { return m_mdl ? MmGetMdlByteCount(m_mdl) : 0; }

        auto next() const { return m_mdl ? m_mdl->Next : nullptr; }
        Mdl& next(_Inout_ Mdl &m);

        bool prepare_nonpaged();
        bool prepare_paged(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode = DEF_ACCESS_MODE);

        bool prepare(_In_ LOCK_OPERATION Operation = IoReadAccess, _In_ KPROCESSOR_MODE AccessMode = DEF_ACCESS_MODE);
        void unprepare();

        auto sysaddr(_In_ ULONG Priority = NormalPagePriority)
        { 
                return m_mdl ? MmGetSystemAddressForMdlSafe(m_mdl, Priority) : nullptr; 
        }

private:
        bool m_paged{};
        MDL *m_mdl{};

        bool lock(_In_ KPROCESSOR_MODE AccessMode, _In_ LOCK_OPERATION Operation);
        auto locked() const { return m_mdl->MdlFlags & MDL_PAGES_LOCKED; }
        void unlock();

        void unprepare_nonpaged() {} // no "undo" operation is required for MmBuildMdlForNonPagedPool
};


size_t list_size(_In_ const Mdl &head);

template<typename MDL1, typename... MDLN>
inline auto prepare(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode, _Inout_ MDL1 &m1, _Inout_ MDLN&... mn)
{
        return (m1.prepare(Operation, AccessMode) && ... && mn.prepare(Operation, AccessMode)); // binary left fold 
}

template<typename MDL1, typename... MDLN>
inline void unprepare(_Inout_ MDL1 &m1, _Inout_ MDLN&... mn)
{
        m1.unprepare();
        (... , mn.unprepare()); // unary left fold 
}

} // namespace usbip