/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include "gcf.h"
#include "u_sstream.h"
#include "u_mem.h"


/*  Query USB info via udevadm
    This works also when /dev/by-id .. is symlinks aren't available

    udevadm info --name=/dev/ttyACM0
*/
static int query_udevadm(Device *dev, Device *end)
{
    FILE *f;
    size_t n;
    U_SStream ss;
    char ch;
    char buf[4096 * 4];
    long udevadm_version;
    unsigned i;
    unsigned dev_major;
    unsigned usb_vendor;

    Device *dev_cur;
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;

    dev_cur = dev;
    udevadm_version = 0;
    /* check if udevadm is available */
    f = popen("udevadm --version", "r");
    if (f)
    {
        n = fread(&buf[0], 1, sizeof(buf) - 1, f);
        if (n > 0 && n < 10)
        {
            buf[n] = '\0';
            U_sstream_init(&ss, &buf[0], (unsigned)n);
            udevadm_version = U_sstream_get_long(&ss);
            if (ss.status != U_SSTREAM_OK)
                udevadm_version = 0;
        }
        pclose(f);
    }

    if (udevadm_version == 0)
        return 0;

    dir = opendir("/dev");

    if (!dir)
        return 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_CHR)
            continue;

        if (dev_cur == end)
            break;

        U_bzero(dev_cur, sizeof(*dev_cur));

        U_sstream_init(&ss, &dev_cur->path[0], sizeof(dev_cur->path));
        U_sstream_put_str(&ss, "/dev/");
        U_sstream_put_str(&ss, &entry->d_name[0]);

        if (stat(ss.str, &statbuf) != 0)
            continue;

        dev_major = statbuf.st_rdev >> 8;
        if (dev_major != 166 && dev_major != 188)
            continue;

        U_sstream_init(&ss, &buf[0], sizeof(buf));
        U_sstream_put_str(&ss, "udevadm info --name=");
        U_sstream_put_str(&ss, "/dev/");
        U_sstream_put_str(&ss, &entry->d_name[0]);

        f = popen(ss.str, "r");
        if (f)
        {
            usb_vendor = 0;
            dev_cur->serial[0] = '\0';
            dev_cur->name[0] = '\0';

            n = fread(&buf[0], 1, sizeof(buf) - 1, f);
            pclose(f);

            if (n > 0 && n < sizeof(buf) - 1)
            {
                buf[n] = '\0';
                U_sstream_init(&ss, &buf[0], (unsigned)n);
                while (U_sstream_find(&ss, "E: ") && U_sstream_remaining(&ss) > 8)
                {
                    ss.pos += 3;
                    if (U_sstream_starts_with(&ss, "ID_USB_VENDOR_ID=") && U_sstream_find(&ss, "="))
                    {
                        ss.pos += 1;
                        if      (U_sstream_starts_with(&ss, "1cf1")) { usb_vendor = 0x1cf1; }
                        else if (U_sstream_starts_with(&ss, "0403")) { usb_vendor = 0x0403; }
                        else if (U_sstream_starts_with(&ss, "1a86")) { usb_vendor = 0x1a86; }
                        else if (U_sstream_starts_with(&ss, "303a")) { usb_vendor = 0x303a; }
                    }
                    else if (U_sstream_starts_with(&ss, "ID_USB_SERIAL_SHORT=") && U_sstream_find(&ss, "="))
                    {
                        i = 0;
                        ss.pos += 1;
                        dev_cur->serial[0] = '\0';

                        for (;ss.pos < ss.len && i + 1 < sizeof(dev_cur->serial); ss.pos++)
                        {
                            ch = U_sstream_peek_char(&ss);
                            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                            {
                                dev_cur->serial[i] = ch;
                                dev_cur->serial[i + 1] = '\0';
                                i++;
                            }
                            else if (ch == ':')
                            {
                                /* 40:4C:CA:42:8B:50 (ignore ':' in hex string) */
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    else if (U_sstream_starts_with(&ss, "ID_USB_MODEL=") && U_sstream_find(&ss, "="))
                    {
                        i = 0;
                        ss.pos += 1;
                        dev_cur->name[0] = '\0';

                        for (;ss.pos < ss.len && i + 1 < sizeof(dev_cur->name); ss.pos++, i++)
                        {
                            ch = U_sstream_peek_char(&ss);
                            if (ch == ' ' || ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                            {
                                dev_cur->name[i] = ch;
                                dev_cur->name[i + 1] = '\0';
                            }
                            else
                            {
                                break;
                            }
                        }

                        {
                            U_SStream s2;
                            U_sstream_init(&s2, dev_cur->name, i);

                            if (U_sstream_starts_with(&s2, "ConBee_III"))
                            {
                                dev_cur->baudrate = PL_BAUDRATE_115200;
                            }
                            else if (U_sstream_starts_with(&s2, "ConBee_II"))
                            {
                                dev_cur->baudrate = PL_BAUDRATE_115200;
                            }
                            else if (U_sstream_starts_with(&s2, "USB_JTAG_serial_debug_unit"))
                            {
                                dev_cur->baudrate = PL_BAUDRATE_115200; /* expressif (FLS-M) */
                            }
                        }
                    }
                }
            }

            if (usb_vendor == 0x1a86)
            {
                dev_cur->baudrate = PL_BAUDRATE_115200;
                if (dev_cur->serial[0] == '\0')
                {
                    /* the CH340 chips don't have a serial? */
                    dev_cur->serial[0] = '1';
                    dev_cur->serial[1] = '\0';
                }
            }
            else if (usb_vendor == 0x303a)
            {
                U_SStream s2;
                U_sstream_init(&s2, dev_cur->name, sizeof(dev_cur->name));
                U_sstream_put_str(&s2, "Espressif");
            }

            if (usb_vendor && dev_cur->serial[0] && dev_cur->name[0])
            {
                U_memcpy(&dev_cur->stablepath[0], &dev_cur->path[0], sizeof(dev_cur->path));
                dev_cur++;
            }
        }

    }

    closedir(dir);

    return (int)(dev_cur - dev);
}

/*! Fills the \p dev array with ConBee I and II devices.

	The array is filled based on the Linux /dev/serial/by-id/ symlinks
	which give a clue about devices without requiring extra libraries.

	\returns The number of devices placed in the array.
 */
int plGetLinuxUSBDevices(Device *dev, Device *end)
{
    Assert(dev < end);

    DIR *dir;
    struct dirent *entry;

    int result = 0;
    char buf[MAX_DEV_PATH_LENGTH];

    result = query_udevadm(dev, end);
    if (result > 0)
        return result;

    Assert(sizeof(dev->stablepath) == sizeof(buf));

    const char *basedir = "/dev/serial/by-id";

    dir = opendir(basedir);

    if (!dir)
    {
        return result;
    }

    const char *devConBeeII = "ConBee_II"; /* usb-dresden_elektronik_ingenieurtechnik_GmbH_ConBee_II_DE1948474-if00 */
    const char *devConBeeIII = "ConBee_III"; /* usb-dresden_elektronik_ConBee_III_DEDEADAFFE-if00-port0 */
    const char *devConBeeIFTDI = "FT230X_Basic_UART"; /* usb-FTDI_FT230X_Basic_UART_DJ00QBWE-if00-port0 */
    const char *devConBeeI = "ConBee";
    const char *devEspressif = "Espressif_USB_JTAG_serial_debug_unit"; /* usb-Espressif_USB_JTAG_serial_debug_unit_40:4C:CA:42:8B:50-if00 */

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

        dev->baudrate = PL_BAUDRATE_UNKNOWN;

        if (strstr(entry->d_name, devConBeeII))
        {
            name = devConBeeII;
            /* usb-dresden_elektronik_ingenieurtechnik_GmbH_ConBee_II_DE1948474-if00
                                                                      ^
            */
            serial = strstr(entry->d_name, devConBeeII) + strlen(devConBeeII) + 1;
            dev->baudrate = PL_BAUDRATE_115200;

        }
        else if (strstr(entry->d_name, devConBeeIII))
        {
            name = devConBeeIII;
            /* usb-dresden_elektronik_ConBee_III_DEDEADAFFE-if00-port0
                                                 ^
            */
            serial = strstr(entry->d_name, devConBeeIII) + strlen(devConBeeIII) + 1;
            dev->baudrate = PL_BAUDRATE_115200;

        }
        else if (strstr(entry->d_name, devConBeeIFTDI))
        {
            name = devConBeeI;
            /* usb-FTDI_FT230X_Basic_UART_DJ00QBWE-if00-port0
                                          ^
            */
            serial = strstr(entry->d_name, devConBeeIFTDI) + strlen(devConBeeIFTDI) + 1;
            dev->baudrate = PL_BAUDRATE_38400;
        }
        else if (strstr(entry->d_name, devEspressif))
        {
            name = "Espressif";
            /* usb-Espressif_USB_JTAG_serial_debug_unit_40:4C:CA:42:8B:50-if00
                                                        ^
            */
            serial = strstr(entry->d_name, devEspressif) + strlen(devEspressif) + 1;
            dev->baudrate = PL_BAUDRATE_115200;
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
        if (snprintf(buf, sizeof(buf), "%s/%s", basedir, entry->d_name) >= (int)sizeof(buf))
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

int plGetLinuxSerialDevices(Device *dev, Device *end)
{
    int sz;
    U_SStream ss;
    struct stat statbuf;
    char lnkpath[MAX_DEV_PATH_LENGTH];
    const char *ser0 = "/dev/serial0";

    if (dev >= end)
        return 0;

    if (lstat(ser0, &statbuf) == 0)
    {
        if (S_ISLNK(statbuf.st_mode))
        {
            sz = readlink(ser0, lnkpath, sizeof(lnkpath) - 1);
            if (sz > 0 && sz < MAX_DEV_PATH_LENGTH - 1)
            {
                lnkpath[sz] = '\0';
                dev->serial[0] = 0;
                U_memcpy(dev->name, "RaspBee", strlen("RaspBee") + 1);
                dev->baudrate = PL_BAUDRATE_38400;

                U_sstream_init(&ss, dev->path, sizeof(dev->path));
                if (lnkpath[0] != '/')
                {
                    U_sstream_put_str(&ss, "/dev/");
                }
                U_sstream_put_str(&ss, lnkpath);
                U_memcpy(dev->stablepath, ser0, strlen(ser0) + 1);
                return 1;
            }
        }
    }

    return 0;
}
