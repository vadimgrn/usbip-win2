/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include "pair.h"
#include <wdm.h>

class unique_ptr
{
        using data = pair<void*, ULONG>;
public:
        unique_ptr() = default;
        unique_ptr(data::first_type ptr, data::second_type tag) : m_pair(ptr, tag) {}
        
        unique_ptr(_In_ POOL_FLAGS Flags, _In_ SIZE_T NumberOfBytes, _In_ ULONG Tag) :
                m_pair(ExAllocatePool2(Flags, NumberOfBytes, Tag), Tag) {}

        auto tag() const { return m_pair.second; }
        auto get() const { return m_pair.first; }

        template<typename T>
        auto get() const { return static_cast<T*>(m_pair.first); }

        ~unique_ptr() 
        { 
                if (auto ptr = get()) {
                        ExFreePoolWithTag(ptr, tag());
                }
        }

        explicit operator bool() const { return m_pair.first; }
        auto operator!() const { return !m_pair.first; }

        auto release()
        {
                auto p = m_pair;
                m_pair = data();
                return p;
        }

        unique_ptr(unique_ptr&& p) : m_pair(p.release()) {}

        auto& operator=(unique_ptr&& p)
        {
                reset(p.release());
                return *this;
        }

        void reset(data::first_type ptr, data::second_type tag)
        {
                if (auto p = get(); p == ptr) {
                        NT_ASSERT(this->tag() == tag);
                        return;
                } else if (p) {
                        ExFreePoolWithTag(p, this->tag());
                }

                m_pair = data(ptr, tag);
        }

        void reset(const data &p = data()) { reset(p.first, p.second); }

        void swap(unique_ptr &p) { m_pair.swap(p.m_pair); }

private:
        data m_pair;
};


inline void swap(unique_ptr &a, unique_ptr &b)
{
        a.swap(b);
}
