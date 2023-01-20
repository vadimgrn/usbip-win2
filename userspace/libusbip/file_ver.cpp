/*
 * Copyright (C) 2001 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "file_ver.h"

#include <sstream>
#include <iomanip>
#include <exception>
#include <cassert>

DWORD FileVersion::SetFile(const std::wstring_view &path)
{
        auto sz = GetFileVersionInfoSize(path.data(), nullptr);
        if (!sz) {
                return GetLastError();
        }

        m_info.resize(sz);

        if (!GetFileVersionInfo(path.data(), 0, sz, m_info.data())) {
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

void *FileVersion::VerQueryValue(const std::wstring &val, UINT &buf_sz) const
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

std::wstring_view FileVersion::VerQueryValue(const wchar_t *val) const
{
        std::wstring_view res;
        auto s = L"\\StringFileInfo\\" + GetTranslation() + L'\\' + val;

        UINT buf_sz;
        auto buf = VerQueryValue(s, buf_sz);

        if (buf && buf_sz) {
                res = std::wstring_view(static_cast<const wchar_t*>(buf), buf_sz);
        }

        return res;
}

std::wstring FileVersion::VerLanguageName() const
{
        WORD wLang = 0;

        std::wistringstream is(std::wstring(m_def_transl, m_def_transl.size() >> 1));
        if (!(is >> std::hex >> wLang)) {
                throw std::exception("FileVersion::VerLanguageName: stream in error state");
        }

	auto cnt = ::VerLanguageName(wLang, 0, 0);
        std::vector<wchar_t> v(cnt);

        cnt = ::VerLanguageName(wLang, v.data(), cnt);
        return std::wstring(v.data(), cnt);
}

std::wstring FileVersion::MakeTransl(DWORD transl)
{
	std::wostringstream os;

	os << std::hex << std::noshowbase << std::setfill(L'0')
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
std::wstring FileVersion::GetTranslation(bool original) const
{
        if (!original) {
                return m_def_transl;
        }

        std::wstring s;

        UINT buf_sz;
        auto buf = VerQueryValue(L"\\VarFileInfo\\Translation", buf_sz);

        if (buf && buf_sz) {
                auto dw = *reinterpret_cast<DWORD*>(buf); // first always must present
                assert(buf_sz == sizeof(dw));
                s = MakeTransl(dw);
        }

        return s;
}

void FileVersion::GetTranslation(WORD &lang_id, UINT &code_page) const
{
        DWORD dw = 0;
        std::wistringstream is(m_def_transl);

        if (!(is >> std::hex >> dw)) {
                throw std::exception("FileVersion::GetTranslation: istream in error state");
        }
        
        lang_id   = HIWORD(dw);
        code_page = LOWORD(dw);
}
