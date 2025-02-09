/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "mdl_cpp.h"

/*
* @see reactos\ntoskrnl\io\iomgr\iomdl.c
*/
usbip::Mdl::Mdl(_In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length) :
        m_mdl(IoAllocateMdl(VirtualAddress, Length, false, false, nullptr))
{
}

/*
 * Impossible to build partial MDL for a chain, only for THIS SourceMdl.
 */
usbip::Mdl::Mdl(_In_ MDL *SourceMdl, _In_ ULONG Offset, _In_ ULONG Length) :
        Mdl((char*)MmGetMdlVirtualAddress(SourceMdl) + Offset, Length)
{
        NT_ASSERT(!SourceMdl->Next);
        NT_ASSERT(Offset + Length <= MmGetMdlByteCount(SourceMdl)); // usbip::size(SourceMdl)

        if (m_mdl) {
                NT_ASSERT(!partial());
                IoBuildPartialMdl(SourceMdl, m_mdl, vaddr(), Length);
                NT_ASSERT(partial());
        }
}

auto usbip::Mdl::operator =(Mdl&& m) -> Mdl&
{
        if (m_mdl != m.m_mdl) {
                reset(m.release());
        }

        return *this;
}

MDL* usbip::Mdl::release()
{
        auto m = m_mdl;
        m_mdl = nullptr;
        return m;
}

void usbip::Mdl::reset(_In_opt_ MDL *mdl)
{
        if (m_mdl) {
                NT_ASSERT(m_mdl != mdl);
                unprepare();
                IoFreeMdl(m_mdl); // calls MmPrepareMdlForReuse
        }

        m_mdl = mdl;
}

NTSTATUS usbip::Mdl::lock(_In_ LOCK_OPERATION Operation)
{
        if (locked()) { // may not lock again until unlock() is called
                return STATUS_ALREADY_COMPLETE;
        }

        __try {
                MmProbeAndLockPages(m_mdl, KernelMode, Operation);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        return locked() ? STATUS_SUCCESS : STATUS_LOCK_NOT_GRANTED;
}

void usbip::Mdl::next(_In_opt_ MDL *m)
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

        if (nonpaged()) {
                return STATUS_ALREADY_COMPLETE;
        }

        MmBuildMdlForNonPagedPool(m_mdl);
        NT_ASSERT(nonpaged());

        return STATUS_SUCCESS;
}

NTSTATUS usbip::Mdl::prepare_paged(_In_ LOCK_OPERATION Operation)
{
        return m_mdl ? lock(Operation) : STATUS_INSUFFICIENT_RESOURCES;
}

/*
 * nonpaged() and partial() can be set both.
 */
void usbip::Mdl::unprepare()
{
        if (locked()) {
                NT_ASSERT(!nonpaged());
                NT_ASSERT(!partial());
                MmUnlockPages(m_mdl);
                NT_ASSERT(!locked());
        } else if (partial()) {
                NT_ASSERT(!locked());
                // MmPrepareMdlForReuse(m_mdl); // IoFreeMdl will call it
        } else if (nonpaged()) { // no "undo" operation is required for MmBuildMdlForNonPagedPool
                NT_ASSERT(!locked());
        }
}

void* usbip::Mdl::sysaddr(_In_ ULONG Priority)
{ 
        return m_mdl ? MmGetSystemAddressForMdlSafe(m_mdl, Priority) : nullptr; 
}

size_t usbip::size(_In_opt_ const MDL *mdl)
{
        size_t total = 0;

        for ( ; mdl; mdl = mdl->Next) {
                total += MmGetMdlByteCount(const_cast<MDL*>(mdl));
        }

        return total;
}

MDL *usbip::tail(_In_opt_ MDL *mdl)
{
        for ( ; mdl && mdl->Next; mdl = mdl->Next);
        return mdl;
}
