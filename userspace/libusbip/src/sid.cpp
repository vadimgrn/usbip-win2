/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "sid.h"
#include "strconv.h"
#include "../win_handle.h"

#include <usbip/hex.h>
#include <sddl.h>

#include <format>

namespace
{

using namespace usbip;

static_assert(se_group_logon_id == SE_GROUP_LOGON_ID);

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

        if (!(use == SidTypeUser || use == SidTypeWellKnownGroup)) {
                return std::unexpected(ERROR_NONE_MAPPED);
        }

        return sid;
}

/*
 * @param s standard SID string notation
 * @note ConvertStringSidToSidA can succeed and still produce an invalid SID
 *       (IsValidSid == FALSE), e.g. wrong revision ("S-2-5-18", "S-0-5-18")
 *       or more than SID_MAX_SUB_AUTHORITIES (15) subauthorities.
 */
HLocal to_sid(_In_ const std::string &s) noexcept
{
        if (PSID sid; ConvertStringSidToSidA(s.c_str(), &sid)) {
                return HLocal(sid);
        }
        return {};
}

/*
 * CopySid only checks destination capacity against GetLengthSid without
 * validating SID revision or subauthority count: it succeeds for wrong
 * revisions and fails with ERROR_INSUFFICIENT_BUFFER (not ERROR_INVALID_SID)
 * when SubAuthorityCount > SID_MAX_SUB_AUTHORITIES.
 * Validating sid upfront ensures ERROR_INVALID_SID is returned consistently
 * and makes is_valid(blob) a guaranteed postcondition.
 */
std::expected<sid_blob, DWORD> to_sid_blob(_In_opt_ PSID sid) noexcept
{
        if (!(sid && IsValidSid(sid))) {
                return std::unexpected(ERROR_INVALID_SID);
        }

        sid_blob blob{};
        if (!CopySid(sizeof(blob.data), blob.data, sid)) {
                return std::unexpected(GetLastError());
        }

        blob.length = GetLengthSid(blob.data);

        assert(is_valid(blob));
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

std::expected<sid_blob, DWORD> get_user_sid(_In_ HANDLE token) noexcept
{
        alignas(TOKEN_USER) char buf[TOKEN_USER_MAX_SIZE];

        if (DWORD actual; !GetTokenInformation(token, TokenUser, buf, sizeof(buf), &actual)) {
                return std::unexpected(GetLastError());
        }

        auto tu = reinterpret_cast<TOKEN_USER*>(buf);
        return to_sid_blob(tu->User.Sid);
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
