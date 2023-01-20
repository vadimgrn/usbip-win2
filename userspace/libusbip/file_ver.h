/*
 * Copyright (C) 2001 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <windows.h>

#include <string>
#include <vector>

class FileVersion 
{
public:
        FileVersion(const std::wstring_view &path) { SetFile(path); }

        explicit operator bool () const { return !m_info.empty(); }
        auto operator !() const { return m_info.empty(); }

        DWORD SetFile(const std::wstring_view &path);
        std::wstring VerLanguageName() const;

        void SetDefTranslation() { m_def_transl = GetTranslation(true); }

        void SetTranslation(WORD lang_id = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), UINT code_page = GetACP());
        void GetTranslation(WORD &lang_id, UINT &code_page) const;
                                              
        auto GetCompanyName() const { return VerQueryValue(L"CompanyName"); }
        auto GetComments() const { return VerQueryValue(L"Comments"); }
        auto GetFileDescription() const { return VerQueryValue(L"FileDescription"); }
        auto GetFileVersion() const { return VerQueryValue(L"FileVersion"); }
        auto GetInternalName() const { return VerQueryValue(L"InternalName"); }
        auto GetLegalCopyright() const { return VerQueryValue(L"LegalCopyright"); }
        auto GetLegalTrademarks() const { return VerQueryValue(L"LegalTrademarks"); }
        auto GetOriginalFilename() const { return VerQueryValue(L"OriginalFilename"); }
        auto GetPrivateBuild() const { return VerQueryValue(L"PrivateBuild"); }
        auto GetProductName() const { return VerQueryValue(L"ProductName"); }
        auto GetProductVersion() const { return VerQueryValue(L"ProductVersion"); }
        auto GetSpecialBuild() const { return VerQueryValue(L"SpecialBuild"); }

private:
        std::vector<wchar_t> m_info;
        std::wstring m_def_transl;

        static DWORD PackTransl(WORD lang_id, UINT code_page);
        static std::wstring MakeTransl(DWORD transl);

        std::wstring GetTranslation(bool original = false) const;

        void *VerQueryValue(const std::wstring &val, UINT &buf_sz) const;
        std::wstring_view VerQueryValue(const wchar_t *val) const;
};
