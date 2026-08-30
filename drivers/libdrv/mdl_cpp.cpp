/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "mdl_cpp.h"

/*
 * @see reactos\ntoskrnl\io\iomgr\iomdl.c
 */
usbip::Mdl::Mdl(_In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length) :
        m_mdl(IoAllocateMdl(VirtualAddress, Length, false, false, nullptr)) {}

/*
 * Impossible to build partial MDL for a chain, only for THIS SourceMdl.
 */
usbip::Mdl::Mdl(_In_ MDL *SourceMdl, _In_ ULONG Offset, _In_ ULONG Length) : m_mdl(nullptr)
{
        if (!SourceMdl) {
                NT_ASSERT(!"SourceMdl is NULL");
                return;
        }

        NT_ASSERT(!SourceMdl->Next);
        NT_ASSERT(static_cast<ULONG64>(Offset) + Length <= MmGetMdlByteCount(SourceMdl));

        auto addr = reinterpret_cast<char*>(MmGetMdlVirtualAddress(SourceMdl)) + Offset;
        m_mdl = IoAllocateMdl(addr, Length, false, false, nullptr);

        if (m_mdl) {
                NT_ASSERT(!partial());
                IoBuildPartialMdl(SourceMdl, m_mdl, addr, Length);
                NT_ASSERT(partial());
        }
}

usbip::Mdl::Mdl(Mdl&& m) :
        m_mdl(m.release()),
        m_mapped(m.m_mapped)
{
        m.m_mapped = false;
}

auto usbip::Mdl::operator =(Mdl&& m) -> Mdl&
{
        if (&m != this && m_mdl != m.m_mdl) {
                reset(m.release(), m.m_mapped);
                m.m_mapped = false;
        }

        return *this;
}

MDL* usbip::Mdl::release()
{
        auto m = m_mdl;
        m_mdl = nullptr;
        return m;
}

void usbip::Mdl::reset(_In_opt_ MDL *mdl, _In_ bool mapped)
{
        if (m_mdl) {
                NT_ASSERT(m_mdl != mdl);
                unprepare();
                IoFreeMdl(m_mdl); // calls MmPrepareMdlForReuse
        }

        m_mdl = mdl;
        m_mapped = mapped;
}

NTSTATUS usbip::Mdl::lock(_In_ LOCK_OPERATION Operation)
{
        NT_ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

        if (!m_mdl) {
                return STATUS_INVALID_PARAMETER;
        }

        if (locked()) { 
                return STATUS_ALREADY_COMPLETE;
        }

        __try {
                MmProbeAndLockPages(m_mdl, KernelMode, Operation);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
                return static_cast<NTSTATUS>(GetExceptionCode());
        }

        NT_ASSERT(locked());
        return STATUS_SUCCESS;
}

void usbip::Mdl::next(_In_opt_ MDL *m)
{ 
        if (m_mdl) {
                NT_ASSERT(m_mdl != m);
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

        if (partial()) { // paged partial, calling BuildMdl is invalid
                return STATUS_INVALID_PARAMETER;
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
        if (!m_mdl) {
                return;
        }

        if (m_mapped) {
                NT_ASSERT(mapped());
                MmUnmapLockedPages(m_mdl->MappedSystemVa, m_mdl);
                m_mapped = false;
                NT_ASSERT(!mapped());
        }

        if (locked()) {
                NT_ASSERT(!nonpaged());
                MmUnlockPages(m_mdl);
                NT_ASSERT(!locked());
        }
}

/*
 * Take ownership of unmapping if the system flag
 * flipped from false to true strictly during this call.
 */
void* usbip::Mdl::sysaddr(_In_ ULONG Priority)
{ 
        if (!m_mdl) {
                return nullptr;
        }

        if (m_mapped) { // skip flag checks, we arleady did it
                return MmGetSystemAddressForMdlSafe(m_mdl, Priority);
        }

        auto was_mapped_by_system = mapped();
        auto addr = MmGetSystemAddressForMdlSafe(m_mdl, Priority);

        if (addr && !was_mapped_by_system && mapped()) {
                m_mapped = true;
        }

        return addr;
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
