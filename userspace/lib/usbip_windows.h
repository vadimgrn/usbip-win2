#pragma once

class InitWinSock2
{
public:
        InitWinSock2();
        ~InitWinSock2();

        InitWinSock2(const InitWinSock2&) = delete;
        InitWinSock2& operator=(const InitWinSock2&) = delete;

        explicit operator bool() const noexcept { return m_ok; }
        auto operator !() const noexcept { return !m_ok; }

private:
        bool m_ok{};
};
