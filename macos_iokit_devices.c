/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include "gcf.h"
#include "u_sstream.h"
#include "u_strlen.h"
#include "u_mem.h"

/*! Enumerates the attached ConBee I, II and III Zigbee USB serial devices.

    The IORegistry is traversed as follows:
      - IOServiceMatching("IOSerialBSDClient") matches the serial client
        nodes. Each of them carries the "IOCalloutDevice" CFString property,
        e.g. "/dev/cu.usbmodemDE21953591" (ConBee II) or
        "/dev/cu.usbserial-DE03189758" (ConBee III).
      - The USB device node carrying the "USB Product Name" CFString
        property (e.g. "ConBee II", "ConBee III" or "FT230X Basic UART"
        for the ConBee I) and the "USB Serial Number" CFString property
        is found by walking up the kIOServicePlane parent chain, since
        the serial client can be several levels below it
        (e.g. IOUSBHostDevice/IOUSBHostInterface/AppleUSBACMData/
        IOSerialBSDClient for the ConBee II).
    Matching is done on the product name only, since the vendor IDs differ
    between the ConBee II (0x1D50) and the ConBee III (0x0403 FTDI).

    \param dev pointer to the first entry of the device array.
    \param end pointer just past the last entry of the device array.

    \returns The number of devices placed in the array.
*/
int plGetMacOSIOKitDevices(Device *dev, Device *end)
{
    CFMutableDictionaryRef match;
    io_iterator_t iter;
    io_service_t service;
    CFMutableDictionaryRef props;
    CFTypeRef callout;
    CFTypeRef product;
    CFTypeRef serial;
    U_SStream ss;
    PL_Baudrate baudrate;
    int ok;
    int count;

    count = 0;
    iter = 0;
    props = NULL;
    callout = NULL;
    product = NULL;
    serial = NULL;
    baudrate = PL_BAUDRATE_38400;

    match = IOServiceMatching("IOSerialBSDClient");
    if (match == NULL)
        return 0;

    /* IOServiceGetMatchingServices consumes one reference of the
     * matching dictionary (CF_RELEASES_ARGUMENT), so it must not be
     * released by the caller afterwards. */
    if (IOServiceGetMatchingServices(0, match, &iter) != kIOReturnSuccess)
    {
        return 0;
    }

    while ((dev < end) && (service = IOIteratorNext(iter)) != 0)
    {
        char calloutBuf[256];
        char productBuf[128];
        char serialBuf[64];
        io_service_t usbe;
        io_service_t next;
        CFTypeRef probe;
        unsigned i;

        ok = 1;
        usbe = service;
        props = NULL;
        product = NULL;
        serial = NULL;

        /* a. Read the properties of the serial client node.
             The dictionary is an out parameter of the call. */
        if (IORegistryEntryCreateCFProperties(service, &props, kCFAllocatorDefault, 0) != kIOReturnSuccess)
        {
            ok = 0;
        }
        else
        {
            /* b. The callout device path, e.g. "/dev/cu.usbmodemDE21953591". */
            callout = CFDictionaryGetValue(props, CFSTR("IOCalloutDevice"));
            if (callout == NULL)
                ok = 0;
        }

        /* c. Walk up the service plane until the USB device carrying
             the "USB Product Name" is found. The serial client can be
             several levels below it, e.g. for the ConBee II:
             IOUSBHostDevice/IOUSBHostInterface@1/AppleUSBACMData/
             IOSerialBSDClient. Every chain node is released exactly
             once, the last one (usbe) at the end of the iteration. */
        if (ok)
        {
            for (i = 0; i < 8; i++)
            {
                probe = IORegistryEntryCreateCFProperty(usbe, CFSTR("USB Product Name"), kCFAllocatorDefault, 0);
                if (probe != NULL)
                {
                    CFRelease(probe);
                    break;
                }
                if (IORegistryEntryGetParentEntry(usbe, kIOServicePlane, &next) != kIOReturnSuccess)
                {
                    ok = 0;
                    break;
                }
                IOObjectRelease(usbe);
                usbe = next;
            }
            if (ok)
            {
                product = IORegistryEntryCreateCFProperty(usbe, CFSTR("USB Product Name"), kCFAllocatorDefault, 0);
                serial  = IORegistryEntryCreateCFProperty(usbe, CFSTR("USB Serial Number"), kCFAllocatorDefault, 0);
            }
        }

        if (ok && (product == NULL || serial == NULL))
            ok = 0;

        /* d. Convert the CFStrings into local fixed size buffers. */
        if (ok)
        {
            if (CFGetTypeID(callout) != CFStringGetTypeID())
                ok = 0;
            else if (CFGetTypeID(product) != CFStringGetTypeID())
                ok = 0;
            else if (CFGetTypeID(serial) != CFStringGetTypeID())
                ok = 0;
            else if (CFStringGetCString(callout, calloutBuf, sizeof(calloutBuf), kCFStringEncodingUTF8) == false)
                ok = 0;
            else if (CFStringGetCString(product, productBuf, sizeof(productBuf), kCFStringEncodingUTF8) == false)
                ok = 0;
            else if (CFStringGetCString(serial, serialBuf, sizeof(serialBuf), kCFStringEncodingUTF8) == false)
                ok = 0;
        }

        if (props != NULL)
            CFRelease(props);
        if (product != NULL)
            CFRelease(product);
        if (serial != NULL)
            CFRelease(serial);
        IOObjectRelease(usbe);

        if (ok == 0)
            continue;

        /* e. Accept only ConBee I, II and III, matched by product name.
             The ConBee I shows up as a plain "FT230X Basic UART". */
        U_sstream_init(&ss, productBuf, U_strlen(productBuf));
        if ((U_sstream_starts_with(&ss, "ConBee") == 0) &&
            (U_sstream_starts_with(&ss, "FT230X Basic UART") == 0))
        {
            continue;
        }

        /* f. ConBee II and III speak 115200, the ConBee I 38400. */
        U_sstream_init(&ss, productBuf, U_strlen(productBuf));
        if ((U_sstream_starts_with(&ss, "ConBee II") != 0) ||
            (U_sstream_starts_with(&ss, "ConBee III") != 0))
        {
            baudrate = PL_BAUDRATE_115200;
        }
        else
        {
            baudrate = PL_BAUDRATE_38400;
        }

        U_memset(dev, 0, sizeof(*dev));
        dev->baudrate = baudrate;

        U_sstream_init(&ss, dev->name, sizeof(dev->name));
        U_sstream_put_str(&ss, productBuf);
        if (ss.status != U_SSTREAM_OK)
            continue;

        U_sstream_init(&ss, dev->serial, sizeof(dev->serial));
        U_sstream_put_str(&ss, serialBuf);
        if (ss.status != U_SSTREAM_OK)
            continue;

        U_sstream_init(&ss, dev->path, sizeof(dev->path));
        U_sstream_put_str(&ss, calloutBuf);
        if (ss.status != U_SSTREAM_OK)
            continue;

        U_memcpy(&dev->stablepath[0], &dev->path[0], sizeof(dev->path));

        /* g. */
        dev++;
        count++;
    }

    IOIteratorReset(iter);
    IOObjectRelease(iter);

    return count;
}
