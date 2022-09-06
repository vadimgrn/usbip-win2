#pragma once

#include <windows.h>

#include <string>
#include <vector>

class FileVersion 
{
public:
        FileVersion(const char *path = nullptr);

        explicit operator bool () const { return !m_info.empty(); }
        auto operator !() const { return m_info.empty(); }

        DWORD SetFile(const char *path);
        std::string VerLanguageName() const;

        void SetDefTranslation() { m_def_transl = GetTranslation(true); }

        void SetTranslation(WORD lang_id = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), UINT code_page = GetACP());
        void GetTranslation(WORD &lang_id, UINT &code_page) const;
                                              
        auto GetCompanyName() const { return VerQueryValue("CompanyName"); }
        auto GetComments() const { return VerQueryValue("Comments"); }
        auto GetFileDescription() const { return VerQueryValue("FileDescription"); }
        auto GetFileVersion() const { return VerQueryValue("FileVersion"); }
        auto GetInternalName() const { return VerQueryValue("InternalName"); }
        auto GetLegalCopyright() const { return VerQueryValue("LegalCopyright"); }
        auto GetLegalTrademarks() const { return VerQueryValue("LegalTrademarks"); }
        auto GetOriginalFilename() const { return VerQueryValue("OriginalFilename"); }
        auto GetPrivateBuild() const { return VerQueryValue("PrivateBuild"); }
        auto GetProductName() const { return VerQueryValue("ProductName"); }
        auto GetProductVersion() const { return VerQueryValue("ProductVersion"); }
        auto GetSpecialBuild() const { return VerQueryValue("SpecialBuild"); }

private:
        std::vector<char> m_info;
        std::string m_def_transl;

        static DWORD PackTransl(WORD lang_id, UINT code_page);
        static std::string MakeTransl(DWORD transl);

        std::string GetTranslation(bool original = false) const;

        void *VerQueryValue(const std::string &val, UINT &buf_sz) const;
        std::string_view VerQueryValue(const char *val) const;
};
