/*****************************************************************************/

/*
 *      names.h  --  USB name database manipulation routines
 *
 *      Copyright (C) 1999, 2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*
 *	Copyright (C) 2005 Takahiro Hirofuchi
 *	       - names_free() is added.
 */

/*****************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* used by usbip_common.c */
const char *names_vendor(uint16_t vendorid);
const char *names_product(uint16_t vendorid, uint16_t productid);
const char *names_class(uint8_t classid);
const char *names_subclass(uint8_t classid, uint8_t subclassid);
const char *names_protocol(uint8_t classid, uint8_t subclassid, uint8_t protocolid);

int  names_init(const char *path);
void names_free();

#ifdef __cplusplus
}
#endif
