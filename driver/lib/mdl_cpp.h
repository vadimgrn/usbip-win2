#pragma once

#include <wdm.h>

namespace usbip
{

enum class memory { nonpaged, paged, stack = paged };

/*
 * Usage:
 * a) prepare()
 * b) sysaddr()
 * c) unprepare()
 */
class Mdl
{
        enum { DEF_ACCESS_MODE = KernelMode };
public:
        Mdl() = default;
        Mdl(_In_ memory pool, _In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length);
        ~Mdl() { reset(); }

        Mdl(const Mdl&) = delete;
        Mdl& operator =(const Mdl&) = delete;

        Mdl(Mdl&& m);
        Mdl& operator =(Mdl&& m);

        MDL *release();
        void reset() { reset(nullptr, memory::nonpaged); }

        explicit operator bool() const { return m_mdl; }
        auto operator !() const { return !m_mdl; }

        auto get() const { return m_mdl; }

        auto addr() const { return m_mdl ? MmGetMdlVirtualAddress(m_mdl) : nullptr; }
        auto offset() const { return m_mdl ? MmGetMdlByteOffset(m_mdl) : 0; }
        auto size() const { return m_mdl ? MmGetMdlByteCount(m_mdl) : 0; }

        auto next() const { return m_mdl ? m_mdl->Next : nullptr; }
        void next(_In_ MDL *m);
        auto& next(_Inout_ Mdl &m) { next(m.get()); return m; }

        NTSTATUS prepare_nonpaged();
        NTSTATUS prepare_paged(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode = DEF_ACCESS_MODE);

        NTSTATUS prepare(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode = DEF_ACCESS_MODE);
        void unprepare();

        auto sysaddr(_In_ ULONG Priority = NormalPagePriority)
        { 
                return m_mdl ? MmGetSystemAddressForMdlSafe(m_mdl, Priority) : nullptr; 
        }

private:
        MDL *m_mdl{};
        bool m_paged{};

        void reset(_In_ MDL *mdl, _In_ memory pool);

        NTSTATUS lock(_In_ KPROCESSOR_MODE AccessMode, _In_ LOCK_OPERATION Operation);
        auto locked() const { return m_mdl->MdlFlags & MDL_PAGES_LOCKED; }
        void unlock();

        void unprepare_nonpaged() {} // no "undo" operation is required for MmBuildMdlForNonPagedPool
};


size_t size(_In_ const Mdl &head);

inline auto& operator +=(_Inout_ Mdl &left, _Inout_ Mdl &right)
{ 
        return left.next(right);
}

template<typename T1, typename... TN>
inline decltype(auto) tie(_Inout_ T1 &t1, _Inout_ TN&... tn)
{
        return (t1 += ... += tn);
}

inline auto prepare(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode, _Inout_ Mdl &m)
{
        return m.prepare(Operation, AccessMode);
}

template<typename T1, typename... TN>
inline auto prepare(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode, _Inout_ T1 &t1, _Inout_ TN&... tn)
{
        if (auto err = prepare(Operation, AccessMode, t1)) {
                return err;
        }

        return prepare(Operation, AccessMode, tn...);
}

template<typename T1, typename... TN>
inline void unprepare(_Inout_ T1 &t1, _Inout_ TN&... tn)
{
        t1.unprepare();
        (... , tn.unprepare()); // unary left fold 
}

} // namespace usbip