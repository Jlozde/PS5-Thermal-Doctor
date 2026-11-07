/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Home-screen tile: a PS5 home-screen entry that opens the web UI.
 */
#ifndef PS5TD_TILE_H
#define PS5TD_TILE_H

enum {
  TILE_OFF = 0,       /* home_tile = 0                                     */
  TILE_WORKING,       /* the background thread is still at it              */
  TILE_PRESENT,       /* it was already on the home screen                 */
  TILE_CREATED,       /* added during this run                             */
  TILE_FAILED,        /* the system refused it (see the code)              */
  TILE_UNAVAILABLE    /* this firmware does not offer the install call     */
};

/* Starts a detached thread that adds the tile if it is missing. The tile
   opens http://127.0.0.1:<port>. Without calling it the state stays OFF. */
void tile_start(unsigned port);

/* Current state; *code = system result code, *port = the port the tile
   opens (0 when unknown), *want = the port this run serves. Any may be NULL. */
int tile_status(unsigned *code, unsigned *port, unsigned *want);

/* "off", "working", "present", "created", "failed", "unavailable" */
const char *tile_state_name(int state);

#endif
