#include "mdl_cpp.h"

/*
* @see reactos\ntoskrnl\io\iomgr\iomdl.c
*/
usbip::Mdl::Mdl(_In_ memory pool, _In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length) :
        m_paged(pool == memory::paged),
        m_mdl(IoAllocateMdl(VirtualAddress, Length, false, false, nullptr))
{
}

usbip::Mdl::Mdl(Mdl&& m) :
        m_paged(m.m_paged),
        m_mdl(m.release())
{
}

auto usbip::Mdl::operator =(Mdl&& m) -> Mdl&
{
        if (m_mdl != m.m_mdl) {
                auto paged = m.m_paged;
                reset(m.release(), paged);
        }

        return *this;
}

MDL* usbip::Mdl::release()
{
        m_paged = -1;

        auto m = m_mdl;
        m_mdl = nullptr;
        return m;
}

void usbip::Mdl::reset(_In_ MDL *mdl, _In_ int paged)
{
        if (m_mdl && managed()) {
                NT_ASSERT(m_mdl != mdl);
                do_unprepare();
                IoFreeMdl(m_mdl);
        }

        m_mdl = mdl;
        m_paged = paged;
}

NTSTATUS usbip::Mdl::lock(_In_ KPROCESSOR_MODE AccessMode, _In_ LOCK_OPERATION Operation)
{
        NT_ASSERT(m_mdl);
        NT_ASSERT(m_paged > 0);

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
        NT_ASSERT(m_paged > 0);

        if (locked()) {
                MmUnlockPages(m_mdl); 
        }
}

void usbip::Mdl::next(_In_ MDL *m)
{ 
        if (m_mdl) {
                m_mdl->Next = m; 
        }
}

NTSTATUS usbip::Mdl::prepare_nonpaged()
{
        if (!m_mdl) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (m_paged) {
                return STATUS_INVALID_DEVICE_REQUEST;
        }

        if (nonpaged_prepared()) {
                return STATUS_ALREADY_COMPLETE;
        }

        MmBuildMdlForNonPagedPool(m_mdl);
        NT_ASSERT(nonpaged_prepared());

        return STATUS_SUCCESS;
}

NTSTATUS usbip::Mdl::prepare_paged(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode)
{
        return !m_mdl ? STATUS_INSUFFICIENT_RESOURCES :
                m_paged > 0 ? lock(AccessMode, Operation) : 
                STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS usbip::Mdl::prepare(_In_ LOCK_OPERATION Operation, _In_ KPROCESSOR_MODE AccessMode)
{
        if (m_mdl && managed()) {
                return m_paged ? prepare_paged(Operation, AccessMode) : prepare_nonpaged();
        }

        return STATUS_INVALID_DEVICE_REQUEST;
}

void usbip::Mdl::unprepare()
{
        if (m_mdl && managed()) {
                do_unprepare();
        }
}

void usbip::Mdl::do_unprepare()
{
        m_paged ? unlock() : unprepare_nonpaged();
}

size_t usbip::size(_In_ const MDL *head)
{
        size_t total = 0;

        for (auto cur = head; cur; cur = cur->Next) {
                total += MmGetMdlByteCount(cur);
        }

        return total;
}
