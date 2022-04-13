/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <guiddef.h>

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip_setupdi.h"

typedef struct {
	unsigned short	vendor, product;
	unsigned char	devno;
} usbdev_t;

typedef struct {
	int		n_usbdevs;
	usbdev_t	*usbdevs;
} usbdev_list_t;

static usbdev_list_t *
create_usbdev_list(void)
{
	usbdev_list_t	*usbdev_list;

	usbdev_list = (usbdev_list_t *)malloc(sizeof(usbdev_list_t));
	if (usbdev_list == nullptr) {
		dbg("create_usbdev_list: out of memory");
		return nullptr;
	}
	usbdev_list->n_usbdevs = 0;
	usbdev_list->usbdevs = nullptr;

	return usbdev_list;
}

static void
add_usbdev(usbdev_list_t *usbdev_list, const char *id_hw, devno_t devno)
{
	unsigned short	vendor, product;
	usbdev_t	*usbdevs, *usbdev;

	if (usbdev_list->n_usbdevs == 255) {
		dbg("exceed maximum usb devices");
		return;
	}
	if (!get_usbdev_info(id_hw, &vendor, &product)) {
		dbg("drop hub or multifunction interface: %s", id_hw);
		return;
	}
	usbdevs = (usbdev_t *)realloc(usbdev_list->usbdevs, sizeof(usbdev_t) * (usbdev_list->n_usbdevs + 1));
	if (usbdevs == nullptr) {
		dbg("out of memory");
		return;
	}
	usbdev_list->n_usbdevs++;
	usbdev = usbdevs + (usbdev_list->n_usbdevs - 1);
	usbdev->vendor = vendor;
	usbdev->product = product;
	usbdev->devno = devno;
	usbdev_list->usbdevs = usbdevs;
}

static void print_device(const char *busid, const char *vendor, const char *product, BOOL parsable)
{
	if (parsable)
		printf("busid=%s#usbid=%.4s:%.4s#", busid, vendor, product);
	else
		printf(" - busid %s (%.4s:%.4s)\n", busid, vendor, product);
}

static void print_product_name(const char *product_name, BOOL parsable)
{
	if (!parsable)
		printf("   %s\n", product_name);
}

static void
list_device(usbdev_t *usbdev, BOOL parsable)
{
	char	busid[128], vendor_id[128], product_id[128];
	char	product_name[128];

	snprintf(busid, 128, "1-%u", (int)usbdev->devno);
	snprintf(vendor_id, 128, "%04hx", usbdev->vendor);
	snprintf(product_id, 128, "%04hx", usbdev->product);

	usbip_names_get_product(product_name, sizeof(product_name), usbdev->vendor, usbdev->product);

	print_device(busid, vendor_id, product_id, parsable);
	print_product_name(product_name, parsable);
}

static int
walker_list(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	usbdev_list_t	*usbdev_list = (usbdev_list_t *)ctx;
	char	*id_hw;

	id_hw = get_id_hw(dev_info, pdev_info_data);
	if (id_hw == nullptr) {
		dbg("failed to get hw id\n");
		return 0;
	}

	add_usbdev(usbdev_list, id_hw, devno);

	free(id_hw);
	return 0;
}

usbdev_list_t *
usbip_list_usbdevs(void)
{
	usbdev_list_t	*usbdev_list;

	usbdev_list = create_usbdev_list();
	if (usbdev_list == nullptr)
		return nullptr;

	traverse_usbdevs(walker_list, TRUE, usbdev_list);

	return usbdev_list;
}

void
usbip_free_usbdev_list(usbdev_list_t *usbdev_list)
{
	if (usbdev_list == nullptr)
		return;
	if (usbdev_list->usbdevs != nullptr)
		free(usbdev_list->usbdevs);
	free(usbdev_list);
}

int list_devices(BOOL parsable)
{
	usbdev_list_t	*usbdev_list;
	int	i;

	usbdev_list = usbip_list_usbdevs();
	if (usbdev_list == nullptr) {
		err("out of memory");
		return 2;
	}

	for (i = 0; i < usbdev_list->n_usbdevs; i++) {
		list_device(usbdev_list->usbdevs + i, parsable);
	}
	usbip_free_usbdev_list(usbdev_list);
	return 0;
}
