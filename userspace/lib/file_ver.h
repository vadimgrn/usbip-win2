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
                                              
        std::string GetCompanyName() const { return VerQueryValue("CompanyName"); }
        std::string GetComments() const { return VerQueryValue("Comments"); }
        std::string GetFileDescription() const { return VerQueryValue("FileDescription"); }
        std::string GetFileVersion() const { return VerQueryValue("FileVersion"); }
        std::string GetInternalName() const { return VerQueryValue("InternalName"); }
        std::string GetLegalCopyright() const { return VerQueryValue("LegalCopyright"); }
        std::string GetLegalTrademarks() const { return VerQueryValue("LegalTrademarks"); }
        std::string GetOriginalFilename() const { return VerQueryValue("OriginalFilename"); }
        std::string GetPrivateBuild() const { return VerQueryValue("PrivateBuild"); }
        std::string GetProductName() const { return VerQueryValue("ProductName"); }
        std::string GetProductVersion() const { return VerQueryValue("ProductVersion"); }
        std::string GetSpecialBuild() const { return VerQueryValue("SpecialBuild"); }

private:
        std::vector<char> m_info;
        std::string m_def_transl;

        static DWORD PackTransl(WORD lang_id, UINT code_page);
        static std::string MakeTransl(DWORD transl);

        std::string GetTranslation(bool original = false) const;

        void *VerQueryValue(const std::string &val, UINT &buf_sz) const;
        std::string VerQueryValue(const std::string &val) const;
};
