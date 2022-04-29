#include "mdl_cpp.h"

/*
* @see reactos\ntoskrnl\io\iomgr\iomdl.c
*/
usbip::Mdl::Mdl(_In_ memory_pool pool, _In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length) :
        m_paged(pool == memory_pool::paged),
        m_mdl(IoAllocateMdl(VirtualAddress, Length, false, false, nullptr))
{
}

usbip::Mdl::~Mdl()
{
        if (m_mdl) {
                unprepare();
                IoFreeMdl(m_mdl);
        }
}

bool usbip::Mdl::lock(_In_ KPROCESSOR_MODE AccessMode, _In_ LOCK_OPERATION Operation)
{
        NT_ASSERT(m_mdl);
        NT_ASSERT(m_paged);

        bool ok = true;

        __try {
                MmProbeAndLockPages(m_mdl, AccessMode, Operation);
                NT_ASSERT(locked());
        } __except (EXCEPTION_EXECUTE_HANDLER) {
                ok = false;
        }

        return ok;
}

void usbip::Mdl::unlock() 
{ 
        NT_ASSERT(m_mdl);
        NT_ASSERT(m_paged);

        if (locked()) {
                MmUnlockPages(m_mdl); 
        }
}

usbip::Mdl& usbip::Mdl::next(_Inout_ Mdl &m)
{ 
        if (m_mdl) {
                m_mdl->Next = m.get(); 
        }

        return m;
}

bool usbip::Mdl::prepare_nonpaged()
{
        auto ok = m_mdl && !m_paged;
        if (ok) {
                MmBuildMdlForNonPagedPool(m_mdl);
        }

        return ok;
}

bool usbip::Mdl::prepare_paged(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode)
{
        return m_mdl && m_paged && lock(AccessMode, Operation);
}

bool usbip::Mdl::prepare(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode)
{
        return !m_mdl ? false :
                m_paged ? lock(AccessMode, Operation) : 
                prepare_nonpaged();
}

void usbip::Mdl::unprepare()
{
        if (m_mdl) {
                m_paged ? unlock() : unprepare_nonpaged();
        }
}

size_t usbip::list_size(_In_ const Mdl &head)
{
        size_t total = 0;

        for (auto cur = head.get(); cur; cur = cur->Next) {
                total += MmGetMdlByteCount(cur);
        }

        return total;
}
