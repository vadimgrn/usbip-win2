/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/
#pragma once

struct usbip_header;

enum { PRINT_BUFSZ = 255 };
const char *print(char *buf, size_t len, const usbip_header &hdr) noexcept;

enum { PRINT_USB_SETUP_BUFBZ = 128 };
const char *print_usb_setup(char *buf, size_t len, const void *packet) noexcept;
