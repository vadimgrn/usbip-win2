#include "usbipd.h"

#include <ws2tcpip.h>

#include "usbip_network.h"

static void
setup_addrinfo_desc(struct addrinfo *ainfo, char buf[], size_t size)
{
	char	hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	if (getnameinfo(ainfo->ai_addr, (socklen_t)ainfo->ai_addrlen, hbuf, sizeof(hbuf),
		sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
		buf[0] = '\0';
	}
	snprintf(buf, size, "%s:%s", hbuf, sbuf);
}

static int
get_n_addrinfos(struct addrinfo *ainfos)
{
	struct addrinfo	*ainfo;
	int		count = 0;

	for (ainfo = ainfos; ainfo != nullptr;  ainfo = ainfo->ai_next) {
		count++;
	}
	return count;
}

static SOCKET
build_sockfd(struct addrinfo *ainfo)
{
	SOCKET	sockfd;
	char	desc[NI_MAXHOST + NI_MAXSERV + 2];

	setup_addrinfo_desc(ainfo, desc, sizeof(desc));
	dbg("opening %s", desc);

	sockfd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
	if (sockfd != INVALID_SOCKET) {
		usbip_net_set_reuseaddr(sockfd);
		usbip_net_set_nodelay(sockfd);
		usbip_net_set_v6only(sockfd);

		if (bind(sockfd, ainfo->ai_addr, (int)ainfo->ai_addrlen) == SOCKET_ERROR) {
			dbg("failed to bind: %s: err: %d", desc, WSAGetLastError());
			closesocket(sockfd);
			return INVALID_SOCKET;
		}

		if (listen(sockfd, SOMAXCONN) == SOCKET_ERROR) {
			dbg("failed to listen: %s: err: %d", desc, WSAGetLastError());
			closesocket(sockfd);
			return INVALID_SOCKET;
		}
		info("listening on %s", desc);
	}
	else {
		dbg("socket error: %s: err: (%d)", desc, WSAGetLastError());
	}

	return sockfd;
}

static SOCKET *
build_sockfds(struct addrinfo *ainfos)
{
	SOCKET	*sockfds;
	struct addrinfo	*ainfo;
	int	n_infos, idx = 0;

	n_infos = get_n_addrinfos(ainfos);
	if (n_infos == 0)
		return nullptr;
	sockfds = (SOCKET *)malloc(sizeof(SOCKET) * (n_infos + 1));
	if (sockfds == nullptr)
		return nullptr;
	for (ainfo = ainfos; ainfo != nullptr; ainfo = ainfo->ai_next) {
		SOCKET sockfd;

		sockfd = build_sockfd(ainfo);
		if (sockfd != INVALID_SOCKET) {
			sockfds[idx] = sockfd;
			idx++;
		}
	}
	if (idx == 0) {
		free(sockfds);
		return nullptr;

	}
	sockfds[idx] = INVALID_SOCKET;
	return sockfds;
}

SOCKET *
get_listen_sockfds(int family)
{
	struct addrinfo		*ainfos, hints;
	SOCKET	*sockfds;
	int	rc;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	rc = getaddrinfo(nullptr, usbip_port_string, &hints, &ainfos);
	if (rc != 0) {
		dbg("failed to get a network address %s: %s", usbip_port_string, gai_strerror(rc));
		return nullptr;
	}

	sockfds = build_sockfds(ainfos);
	freeaddrinfo(ainfos);
	return sockfds;
}