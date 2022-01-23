#pragma once

struct usbip_header;
void trace(const usbip_header &hdr, const char *func, bool remote);
