/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TD_HTTP_H
#define PS5TD_HTTP_H

/* Opens the listener; -2 = port in use, <0 = other error, >=0 = socket. */
int  http_bind(unsigned port);
/* Accept loop; does not return. */
void http_run(int srv);

#endif
