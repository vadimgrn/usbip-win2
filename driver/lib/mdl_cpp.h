#pragma once

#include <wdm.h>

namespace usbip
{

class Mdl
{
public:
        Mdl(_In_opt_ __drv_aliasesMem void *VirtualAddress, _In_ ULONG Length);
        ~Mdl();

        Mdl(const Mdl&) = delete;
        Mdl& operator=(const Mdl&) = delete;

        explicit operator bool() const { return m_mdl; }
        auto operator !() const { return !m_mdl; }

        auto get() const { return m_mdl; }

        auto addr() const { return m_mdl ? MmGetMdlVirtualAddress(m_mdl) : 0; }
        auto offset() const { return m_mdl ? MmGetMdlByteOffset(m_mdl) : 0; }
        auto size() const { return m_mdl ? MmGetMdlByteCount(m_mdl) : 0; }

        bool lock();
        auto locked() const { return m_mdl && (m_mdl->MdlFlags & MDL_PAGES_LOCKED); }
        void unlock();

private:
        MDL *m_mdl{};
};

} // namespace usbip