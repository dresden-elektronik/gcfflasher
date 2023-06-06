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

/* ANSI C module for building and parsing strings.

   The library is standalone without using libc.
   Upstream repository: https://git.sr.ht/~cryo/u_sstream

   Compile time options:

       U_SSTREAM_NO_DEPRECATED   don't compile deprecated functions
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

void U_sstream_init(U_SStream *ss, void *str, unsigned size);
unsigned U_sstream_pos(const U_SStream *ss);
const char *U_sstream_str(const U_SStream *ss);
unsigned U_sstream_remaining(const U_SStream *ss);
int U_sstream_at_end(const U_SStream *ss);
long U_sstream_get_long(U_SStream *ss);
double U_sstream_get_double(U_SStream *ss);
char U_sstream_peek_char(U_SStream *ss);
void U_sstream_skip_whitespace(U_SStream *ss);
int U_sstream_starts_with(U_SStream *ss, const char *str);
int U_sstream_find(U_SStream *ss, const char *str);
void U_sstream_seek(U_SStream *ss, unsigned pos);
void U_sstream_put_str(U_SStream *ss, const char *str);
void U_sstream_put_long(U_SStream *ss, long num);
void U_sstream_put_hex(U_SStream *ss, const void *data, unsigned size);

long U_strtol(const char *s, unsigned len, const char **endp, int *err);
double U_strtod(const char *str, unsigned len, const char **endp, int *err);

#ifndef U_SSTREAM_NO_DEPRECATED

#ifdef __LP64__
  typedef int u_sstream_i32;
  typedef unsigned u_sstream_u32;
#else
  typedef long u_sstream_i32;
  typedef unsigned long u_sstream_u32;
#endif

/* Following functions depend on libc and are defined in u_sstream_lib.c. */
const char *U_sstream_next_token(U_SStream *ss, const char *delim); /* deprecated */
void U_sstream_put_i32(U_SStream *ss, u_sstream_i32 num); /* deprecated */
void U_sstream_put_u32(U_SStream *ss, u_sstream_u32 num); /* deprecated */
u_sstream_i32 U_sstream_get_i32(U_SStream *ss, int base); /* deprecated */
float U_sstream_get_f32(U_SStream *ss);
double U_sstream_get_f64(U_SStream *ss);

#endif /* U_SSTREAM_NO_DEPRECATED */

#endif /* U_SSTREAM_H */
