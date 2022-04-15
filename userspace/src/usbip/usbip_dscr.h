#pragma once

#include "usbip_proto.h"

#include <WinSock2.h>
#include <usbspec.h>

int fetch_device_descriptor(SOCKET sockfd, seqnum_t &seqnum, unsigned int devid, USB_DEVICE_DESCRIPTOR &dd);
int fetch_conf_descriptor(SOCKET sockfd, seqnum_t &seqnum, unsigned int devid, USB_CONFIGURATION_DESCRIPTOR *cfgd, USHORT &wTotalLength);
