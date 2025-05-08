/*
 * Copyright (c) 2021-2025 dresden elektronik ingenieurtechnik gmbh.
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
#include "u_sstream.h"
#include "gcf.h"

/* Implementation for libgpiod version >= 2.x */

/*
   /sys/bus/usb/drivers/cdc_acm
   /sys/class/tty/ttyACM0/device
   /sys/devices/pci0000:00/0000:00:14.0/usb1/1-4/1-4.4/serial
*/

typedef const char *(*pl_gpiod_api_version)(void);
typedef struct gpiod_chip *(*pl_gpiod_chip_open)(const char *path);
typedef void (*pl_gpiod_chip_close)(struct gpiod_chip *chip);
typedef struct gpiod_chip_info *(*pl_gpiod_chip_get_info)(struct gpiod_chip *chip);
typedef void (*pl_gpiod_chip_info_free)(struct gpiod_chip_info *info);
typedef const char *(*pl_gpiod_chip_info_get_label)(struct gpiod_chip_info *info);
typedef struct gpiod_line_request *(*pl_gpiod_chip_request_lines)(struct gpiod_chip *chip, struct gpiod_request_config *req_cfg, struct gpiod_line_config *line_cfg);
typedef struct gpiod_line_config *(*pl_gpiod_line_config_new)(void);
typedef void (*pl_gpiod_line_config_free)(struct gpiod_line_config *config);
typedef void (*pl_gpiod_line_request_release)(struct gpiod_line_request *request);
typedef struct gpiod_line_settings *(*pl_gpiod_line_settings_new)(void);
typedef void (*pl_gpiod_line_settings_free)(struct gpiod_line_settings *settings);
typedef int (*pl_gpiod_line_settings_set_direction)(struct gpiod_line_settings *settings, enum gpiod_line_direction direction);
typedef int (*pl_gpiod_line_settings_set_output_value)(struct gpiod_line_settings *settings, enum gpiod_line_value value);
typedef int (*pl_gpiod_line_config_add_line_settings)(struct gpiod_line_config *config, const unsigned int *offsets, size_t num_offsets, struct gpiod_line_settings *settings);
typedef struct gpiod_request_config *(*pl_gpiod_request_config_new)(void);
typedef void (*pl_gpiod_request_config_free)(struct gpiod_request_config *config);
typedef void (*pl_gpiod_request_config_set_consumer)(struct gpiod_request_config *config, const char *consumer);
typedef int (*pl_gpiod_line_request_set_value)(struct gpiod_line_request *request, unsigned int offset, enum gpiod_line_value value);

static void* lib_gpiod_handle;
static pl_gpiod_api_version          fn_gpiod_api_version;
static pl_gpiod_chip_open            fn_gpiod_chip_open;
static pl_gpiod_chip_close           fn_gpiod_chip_close;
static pl_gpiod_chip_get_info        fn_gpiod_chip_get_info;
static pl_gpiod_chip_info_free       fn_gpiod_chip_info_free;
static pl_gpiod_chip_info_get_label  fn_gpiod_chip_info_get_label;
static pl_gpiod_chip_request_lines   fn_gpiod_chip_request_lines;
static pl_gpiod_line_config_new      fn_gpiod_line_config_new;
static pl_gpiod_line_config_free     fn_gpiod_line_config_free;
static pl_gpiod_line_request_release fn_gpiod_line_request_release;
static pl_gpiod_line_settings_new    fn_gpiod_line_settings_new;
static pl_gpiod_line_settings_free   fn_gpiod_line_settings_free;
static pl_gpiod_line_settings_set_direction fn_gpiod_line_settings_set_direction;
static pl_gpiod_line_settings_set_output_value fn_gpiod_line_settings_set_output_value;
static pl_gpiod_line_config_add_line_settings fn_gpiod_line_config_add_line_settings;

static pl_gpiod_request_config_new fn_gpiod_request_config_new;
static pl_gpiod_request_config_free fn_gpiod_request_config_free;
static pl_gpiod_request_config_set_consumer fn_gpiod_request_config_set_consumer;
static pl_gpiod_line_request_set_value fn_gpiod_line_request_set_value;

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

    fn_gpiod_api_version = (pl_gpiod_api_version)dlsym(lib_gpiod_handle, "gpiod_api_version");

    if (!fn_gpiod_api_version)
    {
        return -1;
    }

    PL_Printf(DBG_DEBUG, "gpiod version %s\n", fn_gpiod_api_version());

    fn_gpiod_chip_open = (pl_gpiod_chip_open)dlsym(lib_gpiod_handle, "gpiod_chip_open");
    fn_gpiod_chip_close = (pl_gpiod_chip_close)dlsym(lib_gpiod_handle, "gpiod_chip_close");
    fn_gpiod_chip_get_info = (pl_gpiod_chip_get_info)dlsym(lib_gpiod_handle, "gpiod_chip_get_info");
    fn_gpiod_chip_info_free = (pl_gpiod_chip_info_free)dlsym(lib_gpiod_handle, "gpiod_chip_info_free");
    fn_gpiod_chip_info_get_label = (pl_gpiod_chip_info_get_label)dlsym(lib_gpiod_handle, "gpiod_chip_info_get_label");
    fn_gpiod_chip_request_lines = (pl_gpiod_chip_request_lines)dlsym(lib_gpiod_handle, "gpiod_chip_request_lines");
    fn_gpiod_line_config_new = (pl_gpiod_line_config_new)dlsym(lib_gpiod_handle, "gpiod_line_config_new");
    fn_gpiod_line_config_free = (pl_gpiod_line_config_free)dlsym(lib_gpiod_handle, "gpiod_line_config_free");
    fn_gpiod_line_request_release = (pl_gpiod_line_request_release)dlsym(lib_gpiod_handle, "gpiod_line_request_release");

    fn_gpiod_line_settings_new = (pl_gpiod_line_settings_new)dlsym(lib_gpiod_handle, "gpiod_line_settings_new");
    fn_gpiod_line_settings_free = (pl_gpiod_line_settings_free)dlsym(lib_gpiod_handle, "gpiod_line_settings_free");
    fn_gpiod_line_settings_set_direction = (pl_gpiod_line_settings_set_direction)dlsym(lib_gpiod_handle, "gpiod_line_settings_set_direction");
    fn_gpiod_line_settings_set_output_value = (pl_gpiod_line_settings_set_output_value)dlsym(lib_gpiod_handle, "gpiod_line_settings_set_output_value");

    fn_gpiod_line_config_add_line_settings = (pl_gpiod_line_config_add_line_settings)dlsym(lib_gpiod_handle, "gpiod_line_config_add_line_settings");

    fn_gpiod_request_config_new = (pl_gpiod_request_config_new)dlsym(lib_gpiod_handle, "gpiod_request_config_new");
    fn_gpiod_request_config_free = (pl_gpiod_request_config_free)dlsym(lib_gpiod_handle, "gpiod_request_config_free");
    fn_gpiod_request_config_set_consumer = (pl_gpiod_request_config_set_consumer)dlsym(lib_gpiod_handle, "gpiod_request_config_set_consumer");
    fn_gpiod_line_request_set_value = (pl_gpiod_line_request_set_value)dlsym(lib_gpiod_handle, "gpiod_line_request_set_value");

    if (!fn_gpiod_chip_open ||
        !fn_gpiod_chip_close ||
        !fn_gpiod_chip_get_info ||
        !fn_gpiod_chip_info_free ||
        !fn_gpiod_chip_info_get_label ||
        !fn_gpiod_chip_request_lines ||
        !fn_gpiod_line_config_new ||
        !fn_gpiod_line_config_free ||
        !fn_gpiod_line_request_release ||
        !fn_gpiod_line_settings_new ||
        !fn_gpiod_line_settings_free ||
        !fn_gpiod_line_settings_set_direction ||
        !fn_gpiod_line_settings_set_output_value ||
        !fn_gpiod_line_config_add_line_settings ||
        !fn_gpiod_request_config_new ||
        !fn_gpiod_request_config_free ||
        !fn_gpiod_request_config_set_consumer ||
        !fn_gpiod_line_request_set_value
        )
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
        fn_gpiod_chip_open = NULL;
        fn_gpiod_chip_close = NULL;
        fn_gpiod_chip_get_info = NULL;
        fn_gpiod_chip_info_free = NULL;
        fn_gpiod_chip_info_get_label = NULL;
        fn_gpiod_chip_request_lines = NULL;
        fn_gpiod_line_config_new = NULL;
        fn_gpiod_line_config_free = NULL;
        fn_gpiod_line_request_release = NULL;

        fn_gpiod_line_settings_new = NULL;
        fn_gpiod_line_settings_free = NULL;
        fn_gpiod_line_settings_set_direction = NULL;
        fn_gpiod_line_settings_set_output_value = NULL;

        fn_gpiod_line_config_add_line_settings = NULL;
        fn_gpiod_request_config_new = NULL;
        fn_gpiod_request_config_free = NULL;
        fn_gpiod_request_config_set_consumer = NULL;
        fn_gpiod_line_request_set_value = NULL;

        return 0;
    }

    return -1;
}

static struct gpiod_chip *plGetGpiodChip(const char *prefix)
{
    int i;
    struct gpiod_chip *chip;
    struct gpiod_chip_info *info;
    const char *label;
    char path[64];
    U_SStream ss;

    for (i = 0; i < 20; i++)
    {
        U_sstream_init(&ss, path, sizeof(path));
        U_sstream_put_str(&ss, "/dev/gpiochip");
        U_sstream_put_long(&ss, i);
        chip = fn_gpiod_chip_open(path);
        if (!chip)
            continue;

        info = fn_gpiod_chip_get_info(chip);
        if (!info)
        {
            fn_gpiod_chip_close(chip);
            continue;
        }

        label = fn_gpiod_chip_info_get_label(info);

        if (label)
        {
            U_sstream_init(&ss, (char*)label, strlen(label));
            if (U_sstream_find(&ss, prefix))
            {
                fn_gpiod_chip_info_free(info);
                return chip;
            }
        }

        fn_gpiod_chip_info_free(info);
        fn_gpiod_chip_close(chip);
    }

    return 0;
}

static struct gpiod_line_request * plRequestOutputLine(struct gpiod_chip *chip, unsigned int offset, enum gpiod_line_value value)
{
    int ret;
    const char *consumer = "gcf";
    struct gpiod_line_request *req = 0;
    struct gpiod_request_config *req_cfg = 0;
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;

    settings = fn_gpiod_line_settings_new();

    if (settings)
    {
        fn_gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
        fn_gpiod_line_settings_set_output_value(settings, value);

        line_cfg = fn_gpiod_line_config_new();

        if (line_cfg)
        {
            ret = fn_gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);

            if (ret == 0)
            {
                if (consumer)
                {
                    req_cfg = fn_gpiod_request_config_new();
                    if (req_cfg)
                        fn_gpiod_request_config_set_consumer(req_cfg, consumer);
                }

                req = fn_gpiod_chip_request_lines(chip, req_cfg, line_cfg);

                if (req_cfg)
                    fn_gpiod_request_config_free(req_cfg);
            }

            fn_gpiod_line_config_free(line_cfg);
        }

        fn_gpiod_line_settings_free(settings);
    }

    return req;
}

int plResetRaspBeeLibGpiod(void)
{
    int ret = -1;
    struct gpiod_chip *chip;
    struct gpiod_line_request *line_req;

    if (plLoadLibGpiod() != 0)
    {
        return -1;
    }

    chip = plGetGpiodChip("pinctrl-");

    if (!chip)
    {
        plUnloadLibGpiod();
        return -2;
    }

    /* https://pinout.xyz/pinout/raspbee
       RaspBee reset pin on gpio17
    */
    line_req = plRequestOutputLine(chip, 17, GPIOD_LINE_VALUE_ACTIVE); /* CBUS0 */

    if (line_req)
    {
        /* TODO not tested yet due lack of libgpoid version 2 for Raspberry Pi */
        ret = fn_gpiod_line_request_set_value(line_req, 0, GPIOD_LINE_VALUE_INACTIVE);
        Assert(ret == 0);
        ret = fn_gpiod_line_request_set_value(line_req, 0, GPIOD_LINE_VALUE_ACTIVE);
        Assert(ret == 0);
        fn_gpiod_line_request_release(line_req);
        ret = 0;
    }

    fn_gpiod_chip_close(chip);
    plUnloadLibGpiod();

    return ret;
}

/* https://web.git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/tree/examples/toggle_line_value.c */
int plResetFtdiLibGpiod(void)
{
    int ret = -1;
    struct gpiod_chip *chip;
    struct gpiod_line_request *line_req;

    if (plLoadLibGpiod() != 0)
    {
        return -1;
    }

    chip = plGetGpiodChip("ftdi-cbus");
    if (!chip)
    {
        plUnloadLibGpiod();
        return -2;
    }

    line_req = plRequestOutputLine(chip, 0, GPIOD_LINE_VALUE_INACTIVE); /* CBUS0 */

    if (line_req)
    {
        ret = fn_gpiod_line_request_set_value(line_req, 0, GPIOD_LINE_VALUE_ACTIVE);
        Assert(ret == 0);
        ret = fn_gpiod_line_request_set_value(line_req, 0, GPIOD_LINE_VALUE_INACTIVE);
        Assert(ret == 0);
        ret = fn_gpiod_line_request_set_value(line_req, 0, GPIOD_LINE_VALUE_ACTIVE);
        Assert(ret == 0);
        fn_gpiod_line_request_release(line_req);
        ret = 0;
    }

    fn_gpiod_chip_close(chip);

    plUnloadLibGpiod();

    return ret;
}

#endif /* HAS_LIBGPIOD */
