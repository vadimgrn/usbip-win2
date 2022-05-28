#pragma once

#include "usbip_api_consts.h"
#include "usbip_proto.h"

#include <ntdef.h>
#include <usbspec.h>

__inline USB_DEFAULT_PIPE_SETUP_PACKET *get_setup(struct usbip_header_cmd_submit *hdr)
{
	static_assert(sizeof(USB_DEFAULT_PIPE_SETUP_PACKET) == sizeof(hdr->setup), "assert");
	return (USB_DEFAULT_PIPE_SETUP_PACKET*)hdr->setup;
}

__inline USB_DEFAULT_PIPE_SETUP_PACKET *get_submit_setup(struct usbip_header *hdr)
{
	return get_setup(&hdr->u.cmd_submit);
}

enum usb_device_speed get_usb_speed(USHORT bcdUSB);
