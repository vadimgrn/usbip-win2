#include "win_socket.h"
#include <spdlog\spdlog.h>

namespace
{

auto init_wsa() noexcept
{
        enum { MAJOR = 2, MINOR = 2 };
        WSADATA	wsaData;

        if (auto err = WSAStartup(MAKEWORD(MINOR, MAJOR), &wsaData)) {
                spdlog::error("WSAStartup error {:#x}", err);
                return false;
        }

        if (!(LOBYTE(wsaData.wVersion) == MINOR && HIBYTE(wsaData.wVersion) == MAJOR)) {
                spdlog::error("cannot find a winsock {}.{} version", MAJOR, MINOR);
                WSACleanup();
                return false;
        }

        return true;
}

} // namespace


usbip::InitWinSock2::InitWinSock2() :
        m_ok(init_wsa())
{
}

usbip::InitWinSock2::~InitWinSock2()
{
        if (m_ok) {
                [[maybe_unused]] auto err = WSACleanup();
                assert(!err);
        }
}
