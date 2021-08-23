/*
 * Copyright (c) 2021 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <dirent.h>

/*! Fills the \p dev array with ConBee I and II devices.

	The array is filled based on the Linux /dev/serial/by-id/ symlinks
	which give a clue about devices without requiring extra libraries.

	\returns The number of devices placed in the array.
 */
static int plGetLinuxUSBDevices(Device *dev, Device *end)
{
    Assert(dev < end);

    DIR *dir;
    struct dirent *entry;

    int result = 0;
    char buf[MAX_DEV_PATH_LENGTH];

    Assert(sizeof(dev->stablepath) == sizeof(buf));

    const char *basedir = "/dev/serial/by-id";

    dir = opendir(basedir);

    if (!dir)
    {
        return result;
    }

    const char *devConBeeII = "ConBee_II"; /* usb-dresden_elektronik_ingenieurtechnik_GmbH_ConBee_II_DE1948474-if00 */
    const char *devConBeeIFTDI = "FT230X_Basic_UART"; /* usb-FTDI_FT230X_Basic_UART_DJ00QBWE-if00-port0 */
    const char *devConBeeI = "ConBee";

    while ((entry = readdir(dir)) != NULL)
    {
        if (dev == end)
        {
            break;
        }

        if (entry->d_name[0] == '.') /* skip . and .. */
        {
            continue;
        }

        const char *name = NULL;
        const char *serial = NULL;

        if (strstr(entry->d_name, devConBeeII))
        {
            name = devConBeeII;
            /* usb-dresden_elektronik_ingenieurtechnik_GmbH_ConBee_II_DE1948474-if00
                                                                      ^
            */
            serial = strstr(entry->d_name, devConBeeII) + strlen(devConBeeII) + 1;

        }
        else if (strstr(entry->d_name, devConBeeIFTDI))
        {
            name = devConBeeI;
            /* usb-FTDI_FT230X_Basic_UART_DJ00QBWE-if00-port0
                                          ^
            */
            serial = strstr(entry->d_name, devConBeeIFTDI) + strlen(devConBeeIFTDI) + 1;
        }

        if (!name)
        {
            continue;
        }

        /* copy name */
        size_t namelen = strlen(name);
        if (namelen >= sizeof(dev->name))
        {
            Assert(!"Device->name buffer too small");
            continue;
        }

        memcpy(dev->name, name, namelen + 1);

        /* copy serial number */
        char *p = NULL;

        dev->serial[0] = '\0';
        if (serial)
        {
            p = strchr(serial, '-');
            Assert(!p || p > serial);
            size_t len = p ? p - serial : 0;
            if (len > 0 && len < sizeof(dev->serial))
            {
                memcpy(dev->serial, serial, len);
                dev->serial[len] = '\0';
            }
        }

        /* copy stable device path */
        if (snprintf(buf, sizeof(buf), "%s/%s", basedir, entry->d_name) >= sizeof(buf))
        {
            Assert(!"Device->stablepath is too small");
            continue;
        }

        memcpy(dev->stablepath, buf, strlen(buf) + 1);

        /* copy path (/dev/ttyACM0 ...) */
        /* realpath writes up to PATH_MAX bytes, and might crash even if the resulting string is smaller */
        char rbuf[PATH_MAX];
        p = realpath(buf, rbuf);

        if (!p)
        {
            Assert(!"failed to get symbolic link target");
            continue;
        }

        if (strlen(p) + 1 < sizeof(dev->path))
        {
        	memcpy(dev->path, p, strlen(p) + 1);
        }

        dev++;
        result++;
    }

    closedir(dir);

    return result;
}
