/*
 * Copyright (c) 2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include "gcf.h"

#define FTDI_VENDOR_ID         0x0403
#define FTDI_FT230X_PRODUCT_ID 0x6015
/* The ConBee III has the same IDs, its product name tells them apart. */
#define CONBEE_1_PRODUCT_NAME  "FT230X Basic UART"

#define FTDI_REQTYPE_VENDOR_OUT 0x40
#define FTDI_SIO_SET_BITMODE    0x0B
#define FTDI_BITMODE_RESET      0x00
#define FTDI_BITMODE_CBUS       0x20
#define FTDI_INDEX_PORT_A       1

static int plPropertyEquals(io_service_t service, CFStringRef key, const char *expected)
{
    CFTypeRef value;
    CFStringRef str;
    int result;

    result = 0;
    value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (value == NULL)
        return 0;

    str = CFStringCreateWithCString(kCFAllocatorDefault, expected, kCFStringEncodingUTF8);
    if (str != NULL)
    {
        if (CFGetTypeID(value) == CFStringGetTypeID())
            result = CFStringCompare((CFStringRef)value, str, 0) == kCFCompareEqualTo;
        CFRelease(str);
    }

    CFRelease(value);
    return result;
}

static void plSetMatchNumber(CFMutableDictionaryRef match, CFStringRef key, SInt32 value)
{
    CFNumberRef number;

    number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    if (number != NULL)
    {
        CFDictionarySetValue(match, key, number);
        CFRelease(number);
    }
}

/*! Finds the ConBee I USB device with the given serial number. */
static io_service_t plFindConBee1(const char *serial)
{
    CFMutableDictionaryRef match;
    io_iterator_t iter;
    io_service_t service;
    io_service_t found;

    found = 0;

    match = IOServiceMatching("IOUSBHostDevice");
    if (match == NULL)
        return 0;

    plSetMatchNumber(match, CFSTR("idVendor"), FTDI_VENDOR_ID);
    plSetMatchNumber(match, CFSTR("idProduct"), FTDI_FT230X_PRODUCT_ID);

    /* consumes one reference of the matching dictionary */
    if (IOServiceGetMatchingServices(0, match, &iter) != kIOReturnSuccess)
        return 0;

    while ((service = IOIteratorNext(iter)) != 0)
    {
        if (found == 0 &&
            plPropertyEquals(service, CFSTR("USB Serial Number"), serial) &&
            plPropertyEquals(service, CFSTR("USB Product Name"), CONBEE_1_PRODUCT_NAME))
        {
            found = service;
        }
        else
        {
            IOObjectRelease(service);
        }
    }

    IOObjectRelease(iter);
    return found;
}

static IOReturn plSetBitMode(IOUSBDeviceInterface **dev, UInt8 mode, UInt8 mask)
{
    IOUSBDevRequest req;

    req.bmRequestType = FTDI_REQTYPE_VENDOR_OUT;
    req.bRequest = FTDI_SIO_SET_BITMODE;
    req.wValue = (UInt16)((mode << 8) | mask);
    req.wIndex = FTDI_INDEX_PORT_A;
    req.wLength = 0;
    req.pData = NULL;
    req.wLenDone = 0;

    return (*dev)->DeviceRequest(dev, &req);
}

/*! Executes a MCU reset for ConBee I via FTDI CBUS0, which is wired to the
    MCU reset pin.

    A vendor request on the default control pipe needs neither the device to be
    opened nor Apple's FTDI serial driver to be detached, since that driver only
    claims the interface. So this works while the serial port stays open, and the
    caller is already listening when the bootloader starts.

    \param serial USB serial number of the device, no other device is touched.

    \returns 0 on success, or -1 if the device isn't found or a request fails.
*/
int plResetMacOSFtdi(const char *serial)
{
    /* high nibble: CBUS direction, low nibble: CBUS level */
    static const UInt8 masks[3] = { 0xF1, 0xF0, 0xF1 };
    io_service_t service;
    IOCFPlugInInterface **plugin;
    IOUSBDeviceInterface **dev;
    SInt32 score;
    HRESULT hres;
    unsigned i;
    int result;

    if (serial == NULL || serial[0] == '\0')
        return -1;

    service = plFindConBee1(serial);
    if (service == 0)
        return -1;

    plugin = NULL;
    dev = NULL;
    result = -1;

    if (IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score) == kIOReturnSuccess && plugin != NULL)
    {
        hres = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&dev);
        (*plugin)->Release(plugin);

        if (hres == S_OK && dev != NULL)
        {
            result = 0;

            for (i = 0; i < sizeof(masks); i++)
            {
                if (plSetBitMode(dev, FTDI_BITMODE_CBUS, masks[i]) != kIOReturnSuccess)
                {
                    result = -1;
                    break;
                }
                PL_MSleep(10);
            }

            /* always give the CBUS pins back, also after a failed request */
            if (plSetBitMode(dev, FTDI_BITMODE_RESET, 0) != kIOReturnSuccess)
                result = -1;

            (*dev)->Release(dev);
        }
    }

    IOObjectRelease(service);
    return result;
}
