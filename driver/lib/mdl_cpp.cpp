#include "mdl_cpp.h"

/*
* @see reactos\ntoskrnl\io\iomgr\iomdl.c
*/
usbip::Mdl::Mdl(_In_ memory pool, _In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length) :
        m_mdl(IoAllocateMdl(VirtualAddress, Length, false, false, nullptr)),
        m_paged(pool == memory::paged)
{
}

usbip::Mdl::~Mdl()
{
        if (m_mdl) {
                unprepare();
                IoFreeMdl(m_mdl);
        }
}

NTSTATUS usbip::Mdl::lock(_In_ KPROCESSOR_MODE AccessMode, _In_ LOCK_OPERATION Operation)
{
        NT_ASSERT(m_mdl);
        NT_ASSERT(m_paged);

        if (locked()) { // may not lock again until unlock() is called
                return STATUS_ALREADY_COMPLETE;
        }

        __try {
                MmProbeAndLockPages(m_mdl, AccessMode, Operation);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        return locked() ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
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

NTSTATUS usbip::Mdl::prepare_nonpaged()
{
        if (!m_mdl) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (m_paged) {
                return STATUS_INVALID_DEVICE_REQUEST;
        }
        
        MmBuildMdlForNonPagedPool(m_mdl);
        return STATUS_SUCCESS;
}

NTSTATUS usbip::Mdl::prepare_paged(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode)
{
        return !m_mdl ? STATUS_INSUFFICIENT_RESOURCES :
                m_paged ? lock(AccessMode, Operation) : 
                STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS usbip::Mdl::prepare(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode)
{
        return m_paged ? prepare_paged(Operation, AccessMode) : prepare_nonpaged();
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
