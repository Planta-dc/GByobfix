/*
 * The status strip on the game picture.
 *
 * A strip of numbers drawn in place over the top of the picture, so that
 * the state of the port can be read at a glance while the game is
 * running: the rate the frames are coming at, how long they take, and
 * what the sound is doing.
 *
 * It is drawn into the framebuffer the picture was just drawn into, which
 * is why it cannot fall behind the picture and cannot need a second
 * present. It covers the top of the picture, which is the price of it.
 */
#pragma once

#include <3ds.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the strip is drawn. */
void hudSetEnabled(bool enabled);
bool hudEnabled(void);

/*
 * Draw the strip.
 *
 * `dest` is the framebuffer the picture was drawn into, `destWidth` the
 * screen's width and `destHeight` its height, and the rectangle is where
 * the picture is on the screen. The framebuffer is on its side (see
 * 3dsgfx.h), so the drawing is done through the same helpers the picture
 * is.
 */
void hudDraw(u16 *dest, int destWidth, int destHeight, int x, int y,
             int width, int height);

#ifdef __cplusplus
}
#endif
