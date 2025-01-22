/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifdef HAS_LIBGPIOD
#include <dlfcn.h>
#include <gpiod.h>
#include <string.h>
#include "gcf.h"

/*
   /sys/bus/usb/drivers/cdc_acm
   /sys/class/tty/ttyACM0/device
   /sys/devices/pci0000:00/0000:00:14.0/usb1/1-4/1-4.4/serial
*/

typedef struct gpiod_chip_iter *(*pl_gpiod_chip_iter_new)(void);
typedef struct gpiod_chip *(*pl_gpiod_chip_iter_next)(struct gpiod_chip_iter *iter);
typedef const char *(*pl_gpiod_chip_name)(struct gpiod_chip *chip);
typedef const char *(*pl_gpiod_chip_label)(struct gpiod_chip *chip);
typedef struct gpiod_line *(*pl_gpiod_chip_get_line)(struct gpiod_chip *chip, unsigned int offset);
typedef int (*pl_gpiod_line_request_output)(struct gpiod_line *line, const char *consumer, int default_val);
typedef int (*pl_gpiod_line_request_input)(struct gpiod_line *line, const char *consumer);
typedef int (*pl_gpiod_line_set_value)(struct gpiod_line *line, int value);
typedef void (*pl_gpiod_line_release)(struct gpiod_line *line);
typedef void (*pl_gpiod_chip_iter_free)(struct gpiod_chip_iter *iter);

static void* lib_gpiod_handle;
static pl_gpiod_chip_iter_new        fn_gpiod_chip_iter_new;
static pl_gpiod_chip_iter_next       fn_gpiod_chip_iter_next;
static pl_gpiod_chip_iter_free       fn_gpiod_chip_iter_free;
static pl_gpiod_chip_name            fn_gpiod_chip_name;
static pl_gpiod_chip_label           fn_gpiod_chip_label;
static pl_gpiod_chip_get_line        fn_gpiod_chip_get_line;
static pl_gpiod_line_request_output  fn_gpiod_line_request_output;
static pl_gpiod_line_request_input  fn_gpiod_line_request_input;
static pl_gpiod_line_set_value       fn_gpiod_line_set_value;
static pl_gpiod_line_release         fn_gpiod_line_release;

static int plLoadLibGpiod(void);
static int plUnloadLibGpiod(void);

static int plLoadLibGpiod(void)
{
    Assert(lib_gpiod_handle == NULL);

    lib_gpiod_handle = dlopen("libgpiod.so", RTLD_LAZY);
    if (!lib_gpiod_handle)
    {
        PL_Printf(DBG_DEBUG, "failed to open libgpiod.so\n");
        return -1;
    }

    fn_gpiod_chip_iter_new = (pl_gpiod_chip_iter_new)dlsym(lib_gpiod_handle, "gpiod_chip_iter_new");
    fn_gpiod_chip_iter_next = (pl_gpiod_chip_iter_next)dlsym(lib_gpiod_handle, "gpiod_chip_iter_next");
    fn_gpiod_chip_iter_free = (pl_gpiod_chip_iter_free)dlsym(lib_gpiod_handle, "gpiod_chip_iter_free");
    fn_gpiod_chip_name = (pl_gpiod_chip_name)dlsym(lib_gpiod_handle, "gpiod_chip_name");
    fn_gpiod_chip_label = (pl_gpiod_chip_label)dlsym(lib_gpiod_handle, "gpiod_chip_label");
    fn_gpiod_chip_get_line = (pl_gpiod_chip_get_line)dlsym(lib_gpiod_handle, "gpiod_chip_get_line");
    fn_gpiod_line_request_output = (pl_gpiod_line_request_output)dlsym(lib_gpiod_handle, "gpiod_line_request_output");
    fn_gpiod_line_request_input = (pl_gpiod_line_request_input)dlsym(lib_gpiod_handle, "gpiod_line_request_input");
    fn_gpiod_line_set_value = (pl_gpiod_line_set_value)dlsym(lib_gpiod_handle, "gpiod_line_set_value");
    fn_gpiod_line_release = (pl_gpiod_line_release)dlsym(lib_gpiod_handle, "gpiod_line_release");

    if (!fn_gpiod_chip_iter_new ||
        !fn_gpiod_chip_iter_next ||
        !fn_gpiod_chip_iter_free ||
        !fn_gpiod_chip_name ||
        !fn_gpiod_chip_label ||
        !fn_gpiod_chip_get_line ||
        !fn_gpiod_line_request_output ||
        !fn_gpiod_line_request_input ||
        !fn_gpiod_line_set_value ||
        !fn_gpiod_line_release)
    {
        plUnloadLibGpiod();
        return -1;
    }

    return 0;
}

static int plUnloadLibGpiod(void)
{
    Assert(lib_gpiod_handle != NULL);

    if (lib_gpiod_handle)
    {
        dlclose(lib_gpiod_handle);
        lib_gpiod_handle = NULL;
        fn_gpiod_chip_iter_new = NULL;
        fn_gpiod_chip_iter_next = NULL;
        fn_gpiod_chip_iter_free = NULL;
        fn_gpiod_chip_name = NULL;
        fn_gpiod_chip_label = NULL;
        fn_gpiod_chip_get_line = NULL;
        fn_gpiod_line_request_output = NULL;
        fn_gpiod_line_request_input = NULL;
        fn_gpiod_line_set_value = NULL;
        fn_gpiod_line_release = NULL;
        return 0;
    }

    return -1;
}

int plResetRaspBeeLibGpiod(void)
{
    int ret = -1;
    struct gpiod_chip *chip;
    struct gpiod_chip_iter *iter;
    struct gpiod_line *line;
    const char *name;
    const char *label;

    if (plLoadLibGpiod() != 0)
    {
        return -1;
    }

    iter = fn_gpiod_chip_iter_new();

    if (!iter)
    {
        plUnloadLibGpiod();
        return -2;
    }

    while ((chip = fn_gpiod_chip_iter_next(iter)) != NULL)
    {
        name = fn_gpiod_chip_name(chip);
        label = fn_gpiod_chip_label(chip);

        if (!label || strncmp(label, "pinctrl-", 8) != 0)
        {
            continue;
        }

        /* https://pinout.xyz/pinout/raspbee
           RaspBee reset pin on gpio17
        */
        line = fn_gpiod_chip_get_line(chip, 17);

        if (!line)
        {
            continue;
        }

        PL_Printf(DBG_DEBUG, "gpiod chip: name: %s, label: %s\n", name, label);

        ret = fn_gpiod_line_request_output(line, "gcf", 1);
        Assert(ret != -1);

        ret = fn_gpiod_line_set_value(line, 0);
        Assert(ret != -1);

        PL_MSleep(200);

        ret = fn_gpiod_line_set_value(line, 1);
        Assert(ret != -1);

        fn_gpiod_line_release(line);

        ret = fn_gpiod_line_request_input(line, "gcf");
        Assert(ret != -1);

        fn_gpiod_line_release(line);

        ret = 0;
        break;
    }

    fn_gpiod_chip_iter_free(iter);

    plUnloadLibGpiod();

    return ret;
}

int plResetFtdiLibGpiod(void)
{
    int ret = -1;
    struct gpiod_chip *chip;
    struct gpiod_chip_iter *iter;
    struct gpiod_line *line;
    const char *name;
    const char *label;

    if (plLoadLibGpiod() != 0)
    {
        return -1;
    }

    iter = fn_gpiod_chip_iter_new();

    if (!iter)
    {
        plUnloadLibGpiod();
        return -2;
    }

    while ((chip = fn_gpiod_chip_iter_next(iter)) != NULL)
    {
        name = fn_gpiod_chip_name(chip);
        label = fn_gpiod_chip_label(chip);

        if (!label || strcmp(label, "ftdi-cbus") != 0)
        {
            continue;
        }

        line = fn_gpiod_chip_get_line(chip, 0); /* CBUS0 */

        if (!line)
        {
            continue;
        }

        PL_Printf(DBG_DEBUG, "gpiod chip: name: %s, label: %s\n", name, label);

        /* toggle CBUS0 which is connected to MCU reset */

        ret = fn_gpiod_line_request_output(line, "gcf", 0);
        Assert(ret != -1);

        ret = fn_gpiod_line_set_value(line, 1);
        Assert(ret != -1);

        ret = fn_gpiod_line_set_value(line, 0);
        Assert(ret != -1);

        ret = fn_gpiod_line_set_value(line, 1);
        Assert(ret != -1);

        fn_gpiod_line_release(line);

        ret = 0;
        break;
    }

    fn_gpiod_chip_iter_free(iter);

    plUnloadLibGpiod();

    return ret;
}
#endif
