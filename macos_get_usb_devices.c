#include <stdio.h>
#include <dirent.h>
#include "gcf.h"
#include "u_sstream.h"
#include "u_strlen.h"
#include "u_mem.h"

/*
   On macOS the following commandline tools can be used:

   system_profiler -json SPUSBDataType
   ioreg -Src IOUSBDevice
*/

char buf[4096 * 64];

static int next_line(U_SStream *line, U_SStream *ss)
{
    if (ss->status != U_SSTREAM_OK)
        return 0;

    line->status = U_SSTREAM_OK;
    line->str = &ss->str[ss->pos];
    line->pos = 0;
    line->len = 0;

    for (;ss->pos < ss->len;)
    {
        line->len++;
        ss->pos++;
        if (line->str[line->len - 1] == '\n')
            break;
    }

    if (line->len)
        return 1;

    return 0;
}

enum query_state
{
    SP_STATE_INIT,
    SP_STATE_DEVICE
};

/*
 * Lookup device path under /dev/
 * The respective entry always has the serial number in the name.
 */
static int getDevicePath(Device *dev)
{
    DIR *dir;
    struct dirent *entry;
    U_SStream ss;

    dir = opendir("/dev");

    if (!dir)
        return 0;

    dev->path[0] = '\0';

    while ((entry = readdir(dir)) != NULL)
    {
        U_sstream_init(&ss, entry->d_name, U_strlen(entry->d_name));

        if (U_sstream_starts_with(&ss, "cu.") == 0)
            continue;

        if (U_sstream_find(&ss, dev->serial))
        {
            U_sstream_init(&ss, dev->path, sizeof(dev->path));
            U_sstream_put_str(&ss, "/dev/");
            U_sstream_put_str(&ss, entry->d_name);

            U_memcpy(&dev->stablepath[0], &dev->path[0], sizeof(dev->path));
            break;
        }
    }

    closedir(dir);

    if (dev->path[0] != '\0')
        return 1;

    return 0;
}

/*
 * Parse the output of: system_profiler -detailLevel mini SPUSBDataType 2>/dev/null
 * It's a bit messy but should also work on older macOS versions before Catalina.
 */
static int queryFromSystemProfiler(Device *dev, Device *end)
{
    FILE *f;
    size_t n;
    U_SStream ss;
    U_SStream line;
    unsigned i;
    enum query_state state;
    int result;

    result = 0;
    ss.str = 0;
    state = SP_STATE_INIT;

    /* check if udevadm is available */
    f = popen("system_profiler -detailLevel mini SPUSBDataType 2>/dev/null", "r");
    if (f)
    {
        n = fread(&buf[0], 1, sizeof(buf) - 1, f);
        if (n > 0)
        {
            buf[n] = '\0';
            U_sstream_init(&ss, &buf[0], (unsigned)n);
        }
        pclose(f);
    }

    if (!ss.str)
        return result;

    for (;;)
    {
        if (dev >= end)
            break;

        if (next_line(&line, &ss) == 0)
            break;

        U_sstream_skip_whitespace(&line);

        if (state == SP_STATE_INIT)
        {
            if (U_sstream_starts_with(&line, "ConBee II:"))
            {
                state = SP_STATE_DEVICE;
                dev->baudrate = PL_BAUDRATE_115200;
            }
            else if (U_sstream_starts_with(&line, "ConBee III:"))
            {
                state = SP_STATE_DEVICE;
                dev->baudrate = PL_BAUDRATE_115200;
            }
            else if (U_sstream_starts_with(&line, "FT230X Basic UART")) /* ConBee I */
            {
                state = SP_STATE_DEVICE;
                dev->baudrate = PL_BAUDRATE_38400;
            }

            if (state == SP_STATE_DEVICE)
            {
                if (line.len - line.pos > sizeof(dev->name))
                {
                    // sanity should not happen
                    state = SP_STATE_INIT;
                    continue;
                }

                for (i = 0; line.str[line.pos + i] != ':'; i++)
                {
                    dev->name[i] = line.str[line.pos + i];
                }

                dev->name[i] = '\0';
            }
        }
        else if (state == SP_STATE_DEVICE)
        {
            if (U_sstream_starts_with(&line, "Serial Number:"))
            {
                state = SP_STATE_INIT;

                for (;line.pos < line.len; line.pos++)
                {
                    if (line.str[line.pos - 1] == ':')
                        break;
                }

                U_sstream_skip_whitespace(&line);

                if (line.len - line.pos > sizeof(dev->serial))
                {
                    // sanity should not happen
                    state = SP_STATE_INIT;
                    continue;
                }

                for (i = 0; line.pos + i < line.len; i++)
                {
                    if (line.str[line.pos + i] == '\n')
                        break;
                    dev->serial[i] = line.str[line.pos + i];
                }

                dev->serial[i] = '\0';

                if (getDevicePath(dev))
                {
                    //printf("'%s' - '%s' (%s)\n", dev->name, dev->serial, dev->path);
                    dev++;
                    result++;
                }
            }
        }
    }

    return result;
}

/*! Fills the \p dev array with ConBee I, II and III devices.

    \returns The number of devices placed in the array.
 */
int plGetMacOSUSBDevices(Device *dev, Device *end)
{
    return queryFromSystemProfiler(dev, end);
}
