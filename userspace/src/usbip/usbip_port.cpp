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
 */

#include "usbip_windows.h"
#include "usbip_common.h"
#include "usbip_vhci.h"
#include <stdlib.h>

static const char usbip_port_usage_string[] =
	"usbip port <args>\n"
	"    -p, --port=<port>      list only given port(for port checking)\n";

void
usbip_port_usage(void)
{
	printf("usage: %s", usbip_port_usage_string);
}

static int
usbip_vhci_imported_device_dump(ioctl_usbip_vhci_imported_dev *idev)
{
	char	product_name[128];

	if (idev->status == VDEV_ST_NULL || idev->status == VDEV_ST_NOTASSIGNED)
		return 0;

	printf("Port %02d: <%s> at %s\n", idev->port, usbip_status_string(idev->status), usbip_speed_string(idev->speed));

	usbip_names_get_product(product_name, sizeof(product_name), idev->vendor, idev->product);

	printf("       %s\n", product_name);

	printf("       ?-? -> unknown host, remote port and remote busid\n");
	printf("           -> remote bus/dev ???/???\n");

	return 0;
}

static int
list_imported_devices(int port)
{
	HANDLE hdev;
        ioctl_usbip_vhci_imported_dev *idevs{};
	BOOL	found = FALSE;
	int	res;
	int	i;

	hdev = usbip_vhci_driver_open();
	if (hdev == INVALID_HANDLE_VALUE) {
		err("failed to open vhci driver");
		return 3;
	}

	res = usbip_vhci_get_imported_devs(hdev, idevs);
	if (res < 0) {
		usbip_vhci_driver_close(hdev);
		err("failed to get attach information");
		return 2;
	}

	printf("Imported USB devices\n");
	printf("====================\n");

	if (usbip_names_init()) {
		dbg("failed to open usb id database");
	}

	for (i = 0; i < 127; i++) {
		if (idevs[i].port < 0)
			break;
		if (port >= 0) {
			if (port != idevs[i].port)
				continue;
			found = TRUE;
		}
		usbip_vhci_imported_device_dump(&idevs[i]);
	}

	free(idevs);

	usbip_vhci_driver_close(hdev);
	usbip_names_free();

	if (port >= 0 && !found) {
		/* port check failed */
		return 2;
	}
	return 0;
}

int
usbip_port_show(int argc, char *argv[])
{
	const option opts[] = {
		{ "port", required_argument, nullptr, 'p' },
		{}
	};

	int port = -1;

	for (;;) {
		int opt = getopt_long(argc, argv, "p:", opts, nullptr);

		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
			if (sscanf_s(optarg, "%d", &port) != 1) {
				err("invalid port: %s", optarg);
				usbip_port_usage();
				return 1;
			}
			break;
		default:
			err("invalid option: %c", opt);
			usbip_port_usage();
			return 1;
		}
	}
	return list_imported_devices(port);
}
