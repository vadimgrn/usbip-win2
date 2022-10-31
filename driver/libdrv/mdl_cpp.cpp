/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

usbip::Mdl::Mdl(Mdl&& m) :
        m_tail(m.m_tail),
        m_mdl(m.release())
{
}

auto usbip::Mdl::operator =(Mdl&& m) -> Mdl&
{
        if (m_mdl != m.m_mdl) {
                auto tail = m.m_tail;
                reset(m.release(), tail);
        }

        return *this;
}

MDL* usbip::Mdl::release()
{
        m_tail = nullptr;

        auto m = m_mdl;
        m_mdl = nullptr;
        return m;
}

void usbip::Mdl::reset(_In_ MDL *mdl, _In_ MDL *tail)
{
        if (m_mdl) {
                if (managed()) {
                        NT_ASSERT(m_mdl != mdl);
                        do_unprepare(false);
                        IoFreeMdl(m_mdl); // calls MmPrepareMdlForReuse
                } else if (m_tail->Next) {
                        m_tail->Next = nullptr;
                }
        }

        m_tail = tail;
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

        if (nonmanaged()) {
                return STATUS_INVALID_DEVICE_REQUEST;
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
        if (!m_mdl) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (nonmanaged()) {
                return STATUS_INVALID_DEVICE_REQUEST;
        }

        return lock(Operation);
}

void usbip::Mdl::unprepare()
{
        if (m_mdl && managed()) {
                do_unprepare(true);
        }
}

/*
 * nonpaged() and partial() can be set both.
 */
void usbip::Mdl::do_unprepare(_In_ bool reuse_partial)
{
        if (locked()) {
                NT_ASSERT(!nonpaged());
                NT_ASSERT(!partial());
                MmUnlockPages(m_mdl);
                NT_ASSERT(!locked());
        } else if (partial()) {
                NT_ASSERT(!locked());
                if (reuse_partial) {
                        MmPrepareMdlForReuse(m_mdl); // it's safe to call several times
                }
        } else if (nonpaged()) { // no "undo" operation is required for MmBuildMdlForNonPagedPool
                NT_ASSERT(!locked());
        }
}

void* usbip::Mdl::sysaddr(_In_ ULONG Priority)
{ 
        return m_mdl ? MmGetSystemAddressForMdlSafe(m_mdl, Priority) : nullptr; 
}

size_t usbip::size(_In_ const MDL *mdl)
{
        size_t total = 0;

        for ( ; mdl; mdl = mdl->Next) {
                total += MmGetMdlByteCount(mdl);
        }

        return total;
}

MDL *usbip::tail(_In_ MDL *mdl)
{
        for ( ; mdl && mdl->Next; mdl = mdl->Next);
        return mdl;
}
