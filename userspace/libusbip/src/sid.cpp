/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "sid.h"
#include "strconv.h"
#include "../win_handle.h"

#include <usbip/hex.h>
#include <sddl.h>

#include <format>
#include <vector>

namespace
{

using namespace usbip;

/*
 * @see libdrv/security.cpp
 */
constexpr auto check_sid_length_range(size_t bytes) noexcept
{
        static_assert(sizeof(sid_blob::data) == SECURITY_MAX_SID_SIZE);

        return  bytes >= offsetof(SID, SubAuthority) &&
                bytes <= sizeof(sid_blob::data);
}

/*
 * @see libdrv::is_valid
 */
auto is_valid(_In_ const sid_blob &sid) noexcept
{
        auto psid = const_cast<UCHAR*>(sid.data);

        return  check_sid_length_range(sid.length) &&
                IsValidSid(psid) &&
                sid.length == GetLengthSid(psid);
}

std::expected<sid_blob, DWORD> lookup_account_name(_In_ const std::wstring &account_name)
{
        if (account_name.empty()) {
                return std::unexpected(ERROR_INVALID_PARAMETER);
        }

        sid_blob sid{};
        DWORD domain_cch{};
        SID_NAME_USE use;

        [[maybe_unused]] auto ok = LookupAccountName(nullptr, account_name.c_str(),
                                        nullptr, &sid.length, nullptr, &domain_cch, &use);

        assert(!ok);
        if (auto err = GetLastError(); err != ERROR_INSUFFICIENT_BUFFER) {
                return std::unexpected(err);
        }

        if (!check_sid_length_range(sid.length)) {
                return std::unexpected(ERROR_INVALID_SID);
        }

        std::wstring domain(domain_cch, L'\0');
        ok = LookupAccountName(nullptr, account_name.c_str(), sid.data, &sid.length, domain.data(), &domain_cch, &use);
        if (!ok) {
                return std::unexpected(GetLastError());
        }

        if (!is_valid(sid)) {
                return std::unexpected(ERROR_INVALID_SID);
        }

        return sid;
}

/*
 * @param s standard SID string notation
 */
HLocal to_sid(_In_ const std::string &s) noexcept
{
        if (PSID sid; ConvertStringSidToSidA(s.c_str(), &sid)) {
                return HLocal(sid);
        }
        return {};
}

std::expected<sid_blob, DWORD> to_sid_blob(_In_ PSID sid) noexcept
{
        assert(IsValidSid(sid));

        sid_blob blob{};
        if (!CopySid(sizeof(blob.data), blob.data, sid)) {
                return std::unexpected(GetLastError());
        }

        blob.length = GetLengthSid(blob.data);

        if (!is_valid(blob)) {
                return std::unexpected(ERROR_INVALID_SID);
        }

        return blob;
}

/*
 * @param sid must point to valid SID
 * @return standard SID string notation
 */
std::string to_string(_In_ PSID sid)
{
        assert(sid);

        std::string str;
        if (LPSTR s; ConvertSidToStringSidA(sid, &s)) {
                HLocal del(s);
                str = s;
        }
        return str;
}

/*
 * @param sid must point to valid SID
 * @return standard SID string notation
 */
std::wstring to_wstring(_In_ PSID sid)
{
        assert(sid);

        std::wstring str;
        if (LPWSTR s; ConvertSidToStringSidW(sid, &s)) {
                HLocal del(s);
                str = s;
        }
        return str;
}

std::expected<sid_blob, DWORD> get_user_sid(_In_ HANDLE token) noexcept
{
        alignas(TOKEN_USER) char buf[TOKEN_USER_MAX_SIZE];

        if (DWORD actual; !GetTokenInformation(token, TokenUser, buf, sizeof(buf), &actual)) {
                return std::unexpected(GetLastError());
        }

        auto tu = reinterpret_cast<TOKEN_USER*>(buf);
        return to_sid_blob(tu->User.Sid);
}

std::optional<std::vector<char>> get_token_info(
        _In_ HANDLE token, _In_ TOKEN_INFORMATION_CLASS cls) noexcept
{
        DWORD req_sz;
        [[maybe_unused]] auto ok = GetTokenInformation(token, cls, nullptr, 0, &req_sz);
        assert(!ok);

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                return std::nullopt;
        }

        if (std::vector<char> v(req_sz);
            GetTokenInformation(token, cls, v.data(), req_sz, &req_sz)) {
                return v;
        }

        return std::nullopt;
}

PSID find_group(_In_ const TOKEN_GROUPS &g, _In_ DWORD flags) noexcept
{
        for (DWORD i = 0; i < g.GroupCount; ++i) {
                if (auto &r = g.Groups[i]; r.Attributes & flags) {
                        return r.Sid;
                }
        }

        return nullptr;
}

} // namespace


auto usbip::get_user_sid() noexcept -> std::expected<sid_blob, DWORD>
{
        return ::get_user_sid(GetCurrentProcessToken());
}

auto usbip::parse_sid(_In_ const std::string &s) -> std::expected<sid_blob, DWORD>
{
        if (auto sid = to_sid(s)) {
                return to_sid_blob(sid.get());
        }

        if (auto ws = utf8_to_wchar(s)) {
                return lookup_account_name(*ws);
        } else {
                return std::unexpected(ws.error());
        }
}

std::string usbip::to_string(_In_ const sid_blob &sid)
{
        assert(is_valid(sid));
        return ::to_string(const_cast<UCHAR*>(sid.data));
}

std::wstring usbip::to_wstring(_In_ const sid_blob &sid)
{
        assert(is_valid(sid));
        return ::to_wstring(const_cast<UCHAR*>(sid.data));
}

std::string usbip::to_string_hex(_In_ const sid_blob &sid)
{
        assert(is_valid(sid));

        std::string hex;
        hex.reserve(2*sid.length);

        for (ULONG i = 0; i < sid.length; ++i) {
                std::format_to(std::back_inserter(hex), "{:02x}", sid.data[i]);
        }

        return hex;
}

auto usbip::parse_sid_hex(_In_ std::wstring_view hex) noexcept -> std::optional<sid_blob>
{
        if (hex.size() % 2) {
                return std::nullopt;
        }

        sid_blob sid {
                .length = static_cast<ULONG>(hex.size()/2)
        };

        if (!check_sid_length_range(sid.length)) {
                return std::nullopt;
        }

        for (ULONG i = 0; i < sid.length; ++i) {

                auto hi = from_hex(hex[2*i]);
                auto lo = from_hex(hex[2*i + 1]);

                if (hi < 0 || lo < 0) {
                        return std::nullopt;
                }

                sid.data[i] = static_cast<UCHAR>((hi << 4) | lo);
        }

        if (is_valid(sid)) {
                return sid;
        }

        return std::nullopt;
}

/*
 * Interactive logon sessions (console, RDP) assign an SE_GROUP_LOGON_ID
 * (S-1-5-5-X-Y) identifying the logon session. Non-interactive or service
 * tokens (such as NT AUTHORITY\SYSTEM in Session 0) do not contain a logon
 * SID, so it is optional.
 */
std::expected<std::wstring, DWORD> usbip::make_caller_aces(_In_ std::wstring_view ace_flags)
{
        auto token = GetCurrentProcessToken();

        auto user_sid = ::get_user_sid(token);
        if (!user_sid) {
                return std::unexpected(user_sid.error());
        }

        auto buf = get_token_info(token, TokenGroups);
        if (!buf) {
                return std::unexpected(GetLastError());
        }
        auto &groups = *reinterpret_cast<TOKEN_GROUPS*>(buf->data());

        constexpr auto &fmt = L"(A;{};GA;;;{})";
        auto aces = std::format(fmt, ace_flags, to_wstring(*user_sid));

        if (auto sid = find_group(groups, SE_GROUP_LOGON_ID)) {
                aces += std::format(fmt, ace_flags, ::to_wstring(sid));
        }

        return aces;
}
