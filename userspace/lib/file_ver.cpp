#include "file_ver.h"

#include <sstream>
#include <iomanip>
#include <exception>
#include <cassert>

FileVersion::FileVersion(const char *path)
{
        SetFile(path ? path : __argv[0]);
}

DWORD FileVersion::SetFile(const char *path)
{
        auto m_info_sz = GetFileVersionInfoSize(path, nullptr);
        if (!m_info_sz) {
                return GetLastError();
        }

        m_info.resize(m_info_sz);

        if (!GetFileVersionInfo(path, 0, m_info_sz, m_info.data())) {
                return GetLastError();
        } 

        SetDefTranslation();
        return 0;
}

void FileVersion::SetTranslation(WORD lang_id, UINT code_page)
{
        auto transl = PackTransl(lang_id, code_page);
        m_def_transl = MakeTransl(transl);
}

void *FileVersion::VerQueryValue(const std::string &val, UINT &buf_sz) const
{
        if (m_info.empty()) {
                throw std::exception("FileVersion::VerQueryValue: not initialized");
        }

        void *buf{};
        buf_sz = 0;

        if (!::VerQueryValue(m_info.data(), val.c_str(), &buf, &buf_sz)) {
                throw std::exception("FileVersion::VerQueryValue error");
        }
        
        return buf;
}

std::string FileVersion::VerQueryValue(const std::string &val) const
{
        std::string res;
        auto s = "\\StringFileInfo\\" + GetTranslation() + '\\' + val;

        UINT buf_sz;
        auto buf = VerQueryValue(s, buf_sz);

        if (buf && buf_sz) {
                res.assign((char*)buf, buf_sz);
        }

        return res;
}

std::string FileVersion::VerLanguageName() const
{
        WORD wLang = 0;

        std::istringstream is(std::string(m_def_transl, m_def_transl.size() >> 1));
        if (!(is >> std::hex >> wLang)) {
                throw std::exception("FileVersion::VerLanguageName: stream in error state");
        }

	auto cnt = ::VerLanguageName(wLang, 0, 0);
        std::vector<char> v(cnt);

        cnt = ::VerLanguageName(wLang, v.data(), cnt);
        return std::string(v.data(), cnt);
}

std::string FileVersion::MakeTransl(DWORD transl)
{
	std::ostringstream os;

	os << std::hex << std::noshowbase << std::setfill('0')
	   << std::setw(sizeof(WORD) << 1) << LOWORD(transl)  // lang_id
           << std::setw(sizeof(WORD) << 1) << HIWORD(transl); // code page

        return os.str();
}

DWORD FileVersion::PackTransl(WORD lang_id, UINT code_page)
{
        auto locale_id = MAKELCID(lang_id, SORT_DEFAULT);

        if (!IsValidLocale(locale_id, LCID_INSTALLED)) { // LCID_SUPPORTED
                throw std::exception("FileVersion::PackTransl: locale is not installed");
        }

        if (!IsValidCodePage(code_page)) {
                throw std::exception("FileVersion::PackTransl: code page is not installed");
        }

        auto offs = static_cast<int>(sizeof(WORD))*CHAR_BIT; // number of bits in WORD

        if (code_page >> offs ) {
                throw std::exception("FileVersion::PackTransl: UINT trancating");
        }

        DWORD res = code_page;
        res <<= offs;

        return res | lang_id;
}

/*
 * if bool original is true, returns translation  (locale ID
 * and code page) obtained from file, otherwise returns value stored
 * by previous call of SetTranslation.
 */
std::string FileVersion::GetTranslation(bool original) const
{
        if (!original) {
                return m_def_transl;
        }

        std::string s;

        UINT buf_sz;
        auto buf = VerQueryValue("\\VarFileInfo\\Translation", buf_sz);

        if (buf && buf_sz > 0) {
                auto dw = *reinterpret_cast<DWORD*>(buf); // first always must present
                assert(buf_sz == sizeof(dw));
                s = MakeTransl(dw);
        }

        return s;
}

void FileVersion::GetTranslation(WORD &lang_id, UINT &code_page) const
{
        DWORD dw = 0;
        std::istringstream is(m_def_transl);

        if (!(is >> std::hex >> dw)) {
                throw std::exception("FileVersion::GetTranslation: istream in error state");
        }
        
        lang_id   = HIWORD(dw);
        code_page = LOWORD(dw);
}
