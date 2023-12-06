/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef NET_H
#define NET_H

int NET_Init(const char *interface, unsigned short port);
int NET_Step(void);
void NET_Exit(void);

/* callback implemented in gcf.c */
void NET_Received(int client_id, const unsigned char *buf, unsigned bufsize);

#endif /* NET_H */
