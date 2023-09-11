/*
 * Copyright (c) 2023 Manuel Pietschmann.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef U_SSTREAM_H
#define U_SSTREAM_H

#ifndef U_LIBAPI
#ifdef _WIN32
  #ifdef USE_ULIB_SHARED
    #define U_LIBAPI  __declspec(dllimport)
  #endif
  #ifdef BUILD_ULIB_SHARED
    #define U_LIBAPI  __declspec(dllexport)
  #endif
#endif
#endif /* ! defined(U_LIBAPI) */

#ifndef U_LIBAPI
#define U_LIBAPI
#endif

/* ANSI C module for building and parsing strings.

   The library is standalone without using libc.
   Upstream repository: https://git.sr.ht/~cryo/u_sstream
*/

typedef enum
{
    U_SSTREAM_OK = 0,
    U_SSTREAM_ERR_RANGE = 1,
    U_SSTREAM_ERR_INVALID = 2,
    U_SSTREAM_ERR_NO_SPACE = 3,
} U_sstream_status;

typedef struct U_SStream
{
    char *str;
    unsigned pos;
    unsigned len;
    U_sstream_status status;
} U_SStream;

#ifdef __cplusplus
extern "C" {
#endif

U_LIBAPI void U_sstream_init(U_SStream *ss, void *str, unsigned size);
U_LIBAPI unsigned U_sstream_pos(const U_SStream *ss);
U_LIBAPI const char *U_sstream_str(const U_SStream *ss);
U_LIBAPI unsigned U_sstream_remaining(const U_SStream *ss);
U_LIBAPI int U_sstream_at_end(const U_SStream *ss);
U_LIBAPI long U_sstream_get_long(U_SStream *ss);
U_LIBAPI double U_sstream_get_double(U_SStream *ss);
U_LIBAPI char U_sstream_peek_char(U_SStream *ss);
U_LIBAPI void U_sstream_skip_whitespace(U_SStream *ss);
U_LIBAPI int U_sstream_starts_with(U_SStream *ss, const char *str);
U_LIBAPI int U_sstream_find(U_SStream *ss, const char *str);
U_LIBAPI void U_sstream_seek(U_SStream *ss, unsigned pos);
U_LIBAPI void U_sstream_put_str(U_SStream *ss, const char *str);

/** Limited JSON friendly double to string conversion.
 *
 * Important: Only the values in range -2^53-1 to 2^53-1 are supported!
 * E.g. [+-]9007199254740991. Values out of this range result in
 * U_SSTREAM_ERR_RANGE status. For full range support use snprintf() instead.
 *
 * Special values:
 *
 *     -0  -->  0
 *    NaN  -->  null
 *   -Inf  --> -1e99999
 *   +Inf  -->  1e99999
 *
 * \param ss the stringt stream context.
 * \param num a double value.
 * \param precision of fractional part (1..18).
 */
U_LIBAPI void U_sstream_put_double(U_SStream *ss, double num, int precision);
U_LIBAPI void U_sstream_put_long(U_SStream *ss, long num);
U_LIBAPI void U_sstream_put_longlong(U_SStream *ss, long long num);
U_LIBAPI void U_sstream_put_ulonglong(U_SStream *ss, unsigned long long num);
U_LIBAPI void U_sstream_put_hex(U_SStream *ss, const void *data, unsigned size);

U_LIBAPI long U_strtol(const char *s, unsigned len, const char **endp, int *err);
U_LIBAPI double U_strtod(const char *str, unsigned len, const char **endp, int *err);

#ifdef __cplusplus
}
#endif

#endif /* U_SSTREAM_H */
