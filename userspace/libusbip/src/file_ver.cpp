/*
 * Copyright (C) 2001 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "file_ver.h"

#include <cassert>
#include <exception>
#include <memory>
#include <vector>
#include <sstream>
#include <iomanip>

class win::FileVersion::Impl
{
public:
        Impl(std::wstring_view path) { SetFile(path); }

        explicit operator bool () const { return !m_info.empty(); }
        auto operator !() const { return m_info.empty(); }

        DWORD SetFile(std::wstring_view path);
        std::wstring VerLanguageName() const;

        void SetDefTranslation() { m_def_transl = GetTranslation(true); }

        void SetTranslation(WORD lang_id, UINT code_page);
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


DWORD win::FileVersion::Impl::SetFile(std::wstring_view path)
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

void win::FileVersion::Impl::SetTranslation(WORD lang_id, UINT code_page)
{
        auto transl = PackTransl(lang_id, code_page);
        m_def_transl = MakeTransl(transl);
}

void *win::FileVersion::Impl::VerQueryValue(const std::wstring &val, UINT &buf_sz) const
{
        if (m_info.empty()) {
                throw std::exception("win::FileVersion::VerQueryValue: not initialized");
        }

        void *buf{};
        buf_sz = 0;

        if (!::VerQueryValue(m_info.data(), val.c_str(), &buf, &buf_sz)) {
                throw std::exception("win::FileVersion::VerQueryValue error");
        }
        
        return buf;
}

std::wstring_view win::FileVersion::Impl::VerQueryValue(const wchar_t *val) const
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

std::wstring win::FileVersion::Impl::VerLanguageName() const
{
        WORD wLang = 0;

        std::wistringstream is(std::wstring(m_def_transl, m_def_transl.size() >> 1));
        if (!(is >> std::hex >> wLang)) {
                throw std::exception("win::FileVersion::VerLanguageName: stream in error state");
        }

	auto cnt = ::VerLanguageName(wLang, 0, 0);
        std::vector<wchar_t> v(cnt);

        cnt = ::VerLanguageName(wLang, v.data(), cnt);
        return std::wstring(v.data(), cnt);
}

std::wstring win::FileVersion::Impl::MakeTransl(DWORD transl)
{
	std::wostringstream os;

	os << std::hex << std::noshowbase << std::setfill(L'0')
	   << std::setw(sizeof(WORD) << 1) << LOWORD(transl)  // lang_id
           << std::setw(sizeof(WORD) << 1) << HIWORD(transl); // code page

        return os.str();
}

DWORD win::FileVersion::Impl::PackTransl(WORD lang_id, UINT code_page)
{
        auto locale_id = MAKELCID(lang_id, SORT_DEFAULT);

        if (!IsValidLocale(locale_id, LCID_INSTALLED)) { // LCID_SUPPORTED
                throw std::exception("win::FileVersion::PackTransl: locale is not installed");
        }

        if (!IsValidCodePage(code_page)) {
                throw std::exception("win::FileVersion::PackTransl: code page is not installed");
        }

        auto offs = static_cast<int>(sizeof(WORD))*CHAR_BIT; // number of bits in WORD

        if (code_page >> offs ) {
                throw std::exception("win::FileVersion::PackTransl: UINT trancating");
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
std::wstring win::FileVersion::Impl::GetTranslation(bool original) const
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

void win::FileVersion::Impl::GetTranslation(WORD &lang_id, UINT &code_page) const
{
        DWORD dw = 0;
        std::wistringstream is(m_def_transl);

        if (!(is >> std::hex >> dw)) {
                throw std::exception("win::FileVersion::GetTranslation: istream in error state");
        }
        
        lang_id   = HIWORD(dw);
        code_page = LOWORD(dw);
}

win::FileVersion::FileVersion(std::wstring_view path) : m_impl(new Impl(path)) {}
win::FileVersion::~FileVersion() { delete m_impl; }

auto win::FileVersion::operator =(FileVersion&& obj) -> FileVersion&
{
        if (&obj != this) {
                delete m_impl;
                m_impl = obj.release();
        }

        return *this;
}

win::FileVersion::operator bool () const { return static_cast<bool>(*m_impl); }
bool win::FileVersion::operator !() const { return !*m_impl; }

DWORD win::FileVersion::SetFile(std::wstring_view path) { return m_impl->SetFile(path); }
std::wstring win::FileVersion::VerLanguageName() const { return m_impl->VerLanguageName(); }

void win::FileVersion::SetDefTranslation() { m_impl->SetDefTranslation(); }
void win::FileVersion::SetTranslation(WORD lang_id, UINT code_page) { m_impl->SetTranslation(lang_id, code_page); }
void win::FileVersion::GetTranslation(WORD &lang_id, UINT &code_page) const { m_impl->GetTranslation(lang_id, code_page); }

std::wstring_view win::FileVersion::GetCompanyName() const { return m_impl->GetCompanyName(); }
std::wstring_view win::FileVersion::GetComments() const { return m_impl->GetComments(); }
std::wstring_view win::FileVersion::GetFileDescription() const { return m_impl->GetFileDescription(); }
std::wstring_view win::FileVersion::GetFileVersion() const { return m_impl->GetFileVersion(); }
std::wstring_view win::FileVersion::GetInternalName() const { return m_impl->GetInternalName(); }
std::wstring_view win::FileVersion::GetLegalCopyright() const { return m_impl->GetLegalCopyright(); }
std::wstring_view win::FileVersion::GetLegalTrademarks() const { return m_impl->GetLegalTrademarks(); }
std::wstring_view win::FileVersion::GetPrivateBuild() const { return m_impl->GetPrivateBuild(); }
std::wstring_view win::FileVersion::GetProductName() const { return m_impl->GetProductName(); }
std::wstring_view win::FileVersion::GetProductVersion() const { return m_impl->GetProductVersion(); }
std::wstring_view win::FileVersion::GetSpecialBuild() const { return m_impl->GetSpecialBuild(); }
