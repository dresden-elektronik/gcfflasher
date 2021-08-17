/*
 * Copyright (c) 2021 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifdef HAS_LIBFTDI

#include <ftdi.h>

int plResetLibFtdi()
{
    int ret;
    struct ftdi_context *ftdi;

    if ((ftdi = ftdi_new()) == NULL)
    {
        fprintf(stderr, "ftdi_new failed\n");
        return -1;
    }

    ftdi->module_detach_mode = AUTO_DETACH_REATACH_SIO_MODULE;

    ret = ftdi_usb_open(ftdi, 0x0403, 0x6015);
    if (ret < 0 && ret != -5)
    {
        fprintf(stderr, "unable to open ftdi device: %d (%s)\n", ret, ftdi_get_error_string(ftdi));
        ftdi_deinit(ftdi);
        return -2;
    }

    uint8_t bitmask[3] = { 0xf1, 0xf0, 0xf1 };

    for (uint8_t i = 0 ; i < sizeof(bitmask); i++)
    {
        ret = ftdi_set_bitmode(ftdi, bitmask[i], BITMODE_CBUS);
        if (ret < 0)
        {
            fprintf(stderr, "set_bitmode failed for 0x%x, error %d (%s)\n", bitmask[i], ret, ftdi_get_error_string(ftdi));
            break;
        }

        PL_MSleep(10);

        // read CBUS
        uint8_t buf;
        ret = ftdi_read_pins(ftdi, &buf);
        if (ret < 0)
        {
            fprintf(stderr, "read_pins failed, error %d (%s)\n", ret, ftdi_get_error_string(ftdi));
            continue;
        }
        printf("read returned 0x%02x\n", buf);
    }

    // ftdi_set_bitmode(ftdi, 0, 0x00); // RESET


    printf("disabling bitbang mode\n");
    ftdi_disable_bitbang(ftdi);

    ftdi_usb_close(ftdi);
    ftdi_free(ftdi);

    return 0;
}
#endif
