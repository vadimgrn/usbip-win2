/*
 * Copyright (c) 2001-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "file_ver.h"

#include <cassert>
#include <system_error>
#include <vector>
#include <format>

 /*
  * @see _get_pgmptr, _get_wpgmptr
  */
std::wstring win::get_module_filename()
{
        std::wstring path(MAX_PATH, L'\0');

        for (auto ok = true; ok; ) {
                auto cch = GetModuleFileName(nullptr, path.data(), static_cast<DWORD>(path.size()));

                if (ok = cch == path.size(); ok) {
                        assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
                        cch *= 2;
                }

                path.resize(cch);
        }

        return path;
}

namespace
{

DWORD parse_transl(const std::wstring &s)
{
        wchar_t *end{};
        auto dw = wcstoul(s.c_str(), &end, 16);
        if (s.empty() || end != s.c_str() + s.size()) {
                throw std::runtime_error("FileVersion: invalid translation string");
        }
        return dw;
}

} // namespace

class win::FileVersion::Impl
{
public:
        Impl() = default;
        Impl(std::wstring_view filename);

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
        std::vector<char> m_info;
        std::wstring m_def_transl;

        static DWORD PackTransl(WORD lang_id, UINT code_page);
        static std::wstring MakeTransl(DWORD transl);

        std::wstring GetTranslation(bool original = false) const;

        const void *VerQueryValue(const std::wstring &val, UINT &buf_sz) const noexcept;
        std::wstring_view VerQueryValue(const wchar_t *value) const;
};

win::FileVersion::Impl::Impl(std::wstring_view filename)
{
        if (auto err = SetFile(filename)) {
                throw std::system_error(err, std::system_category(), "FileVersion::SetFile");
        }
}

DWORD win::FileVersion::Impl::SetFile(std::wstring_view path)
{
        if (path.empty()) {
                return ERROR_INVALID_PARAMETER;
        }

        std::wstring null_term_path(path);
        auto sz = GetFileVersionInfoSize(null_term_path.c_str(), nullptr);
        if (!sz) {
                return GetLastError();
        }

        std::vector<char> info(sz);

        if (!GetFileVersionInfo(null_term_path.c_str(), 0, sz, info.data())) {
                return GetLastError();
        }

        m_info = std::move(info);
        SetDefTranslation();
        return ERROR_SUCCESS;
}

void win::FileVersion::Impl::SetTranslation(WORD lang_id, UINT code_page)
{
        auto transl = PackTransl(lang_id, code_page);
        m_def_transl = MakeTransl(transl);
}

const void *win::FileVersion::Impl::VerQueryValue(const std::wstring &val, UINT &buf_sz) const noexcept
{
        void *buf{};
        buf_sz = 0;

        if (!m_info.empty()) {
                ::VerQueryValue(m_info.data(), val.c_str(), &buf, &buf_sz);
        }

        return buf;
}

std::wstring_view win::FileVersion::Impl::VerQueryValue(const wchar_t *value) const
{
        std::wstring_view res;
        auto transl = GetTranslation();
        if (transl.empty()) {
                return res;
        }

        auto key = L"\\StringFileInfo\\" + transl + L'\\' + value;

        UINT cch{};
        auto ws = static_cast<const wchar_t*>(VerQueryValue(key, cch));

        if (ws && cch) {
                if (ws[cch - 1] == L'\0') {
                        --cch;
                }
                res = std::wstring_view(ws, cch);
        }

        return res;
}

std::wstring win::FileVersion::Impl::VerLanguageName() const
{
        if (m_def_transl.empty()) {
                return {};
        }

        auto wLang = HIWORD(parse_transl(m_def_transl));

        wchar_t buf[256];
        auto cch = ::VerLanguageName(wLang, buf, std::size(buf));

        return std::wstring(buf, std::min<size_t>(cch, std::size(buf) - 1));
}

std::wstring win::FileVersion::Impl::MakeTransl(DWORD transl)
{
        return std::format(L"{:04x}{:04x}", LOWORD(transl), HIWORD(transl));
}

/*
 * IsValidLocale and IsValidCodePage are intentionally not used:
 * - IsValidLocale rejects LANG_NEUTRAL (0x0000, the default in SetTranslation)
 *   as well as language IDs present in a file's version resource that are not
 *   installed on the current system.
 * - IsValidCodePage always returns FALSE for Unicode code pages such as 1200
 *   (0x04b0, UTF-16LE), which is the standard code page in Win32 version resources.
 */
DWORD win::FileVersion::Impl::PackTransl(WORD lang_id, UINT code_page)
{
        constexpr auto offs = static_cast<int>(sizeof(WORD))*CHAR_BIT; // number of bits in WORD

        if (code_page >> offs) {
                auto s = std::format("FileVersion::PackTransl: UINT({:#x}) truncating", code_page);
                throw std::overflow_error(s);
        }

        return MAKELONG(lang_id, static_cast<WORD>(code_page));
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

        UINT buf_sz{};
        auto ptr = reinterpret_cast<const DWORD*>(VerQueryValue(L"\\VarFileInfo\\Translation", buf_sz)); // first always must be present

        if (ptr && buf_sz) {
                assert(buf_sz >= sizeof(*ptr));
                s = MakeTransl(*ptr);
        }

        return s;
}

void win::FileVersion::Impl::GetTranslation(WORD &lang_id, UINT &code_page) const
{
        auto dw = m_def_transl.empty() ? 0 : parse_transl(m_def_transl);
        lang_id   = HIWORD(dw);
        code_page = LOWORD(dw);
}

win::FileVersion::FileVersion(const std::wstring &filename) : m_impl(new Impl(filename)) {}
win::FileVersion::~FileVersion() { delete m_impl; }

auto win::FileVersion::operator =(FileVersion&& obj) noexcept -> FileVersion&
{
        if (&obj != this) {
                delete m_impl;
                m_impl = obj.release();
        }

        return *this;
}

win::FileVersion::operator bool () const { return m_impl && static_cast<bool>(*m_impl); }
bool win::FileVersion::operator !() const { return !bool(*this); }

DWORD win::FileVersion::SetFile(std::wstring_view path)
{
        if (!m_impl) {
                m_impl = new Impl;
        }
        return m_impl->SetFile(path);
}

std::wstring win::FileVersion::VerLanguageName() const { return m_impl ? m_impl->VerLanguageName() : std::wstring{}; }

void win::FileVersion::SetDefTranslation()
{
        if (m_impl) {
                m_impl->SetDefTranslation();
        }
}

void win::FileVersion::SetTranslation(WORD lang_id, UINT code_page)
{
        if (m_impl) {
                m_impl->SetTranslation(lang_id, code_page);
        }
}

void win::FileVersion::GetTranslation(WORD &lang_id, UINT &code_page) const
{
        if (m_impl) {
                m_impl->GetTranslation(lang_id, code_page);
        } else {
                lang_id = 0;
                code_page = 0;
        }
}

std::wstring_view win::FileVersion::GetCompanyName() const { return m_impl ? m_impl->GetCompanyName() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetComments() const { return m_impl ? m_impl->GetComments() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetFileDescription() const { return m_impl ? m_impl->GetFileDescription() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetFileVersion() const { return m_impl ? m_impl->GetFileVersion() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetInternalName() const { return m_impl ? m_impl->GetInternalName() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetLegalCopyright() const { return m_impl ? m_impl->GetLegalCopyright() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetLegalTrademarks() const { return m_impl ? m_impl->GetLegalTrademarks() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetOriginalFilename() const { return m_impl ? m_impl->GetOriginalFilename() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetPrivateBuild() const { return m_impl ? m_impl->GetPrivateBuild() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetProductName() const { return m_impl ? m_impl->GetProductName() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetProductVersion() const { return m_impl ? m_impl->GetProductVersion() : std::wstring_view{}; }
std::wstring_view win::FileVersion::GetSpecialBuild() const { return m_impl ? m_impl->GetSpecialBuild() : std::wstring_view{}; }
