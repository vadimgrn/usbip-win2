#include "mdl_cpp.h"

/*
* @see reactos\ntoskrnl\io\iomgr\iomdl.c
*/
usbip::Mdl::Mdl(_In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length) :
        m_mdl(IoAllocateMdl(VirtualAddress, Length, false, false, nullptr))
{
}

usbip::Mdl::~Mdl()
{
        if (*this) {
                unlock();
                IoFreeMdl(m_mdl);
        }
}

bool usbip::Mdl::lock()
{
        if (!*this) {
                return false;
        }

        bool ok = true;

        __try {
                MmProbeAndLockPages(m_mdl, KernelMode, IoWriteAccess);
                NT_ASSERT(locked());
        } __except (EXCEPTION_EXECUTE_HANDLER) {
                ok = false;
        }

        return ok;
}

void usbip::Mdl::unlock() 
{ 
        if (locked()) {
                MmUnlockPages(m_mdl); 
        }
}