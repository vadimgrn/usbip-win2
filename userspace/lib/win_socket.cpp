#include "win_socket.h"
#include "usbip_common.h"

namespace
{

auto init_wsa() noexcept
{
        enum { MAJOR = 2, MINOR = 2 };
        WSADATA	wsaData;

        if (auto err = WSAStartup(MAKEWORD(MINOR, MAJOR), &wsaData)) {
                dbg("WSAStartup error %#x", err);
                return false;
        }

        if (!(LOBYTE(wsaData.wVersion) == MINOR && HIBYTE(wsaData.wVersion) == MAJOR)) {
                dbg("cannot find a winsock %d.%d version", MAJOR, MINOR);
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
