#pragma once

class InitUsbNames
{
public:
        InitUsbNames();
        ~InitUsbNames();

        InitUsbNames(const InitUsbNames&) = delete;
        InitUsbNames& operator=(const InitUsbNames&) = delete;

        explicit operator bool() const noexcept { return m_ok; }
        auto operator !() const noexcept { return !m_ok; }

private:
        bool m_ok{};
};

