# The 3DS port's performance

This is what the port does about being slow, what was measured, and what
has **not** been measured.

The last part matters as much as the first. Everything below was measured
on a development machine, because that is the only machine this was built
on. The 3DS is a different machine with a different cache and a different
memory system, and the numbers here are not the numbers the console will
see. What they are good for is the shape of the result: which part of the
frame's time went where, and how much of it is left.

## The complaint

The 3DS build ran slower than the DS build. The DS build draws its
pictures with the same code the 3DS build used to use, and the two
emulators run the same Game Boy core, so the difference was not the
emulation: it was what the 3DS port did around it.

There were three things.

**The renderer drew the wrong way.** Every pixel of the picture was
written straight into the 3DS framebuffer, one pixel at a time, three
bytes at a time, into a framebuffer that is turned on its side. The
picture is 23040 pixels; the framebuffer holds the picture a column at a
time, so two pixels that are next to each other on the screen are 720
bytes apart in memory. Every pixel was therefore a cache miss, and every
one of them was a write to memory the display controller reads over the
bus.

**The frame was paced on the wrong clock.** The port waited for the
display's vertical blank. The Game Boy runs at 59.7275 frames a second and
the 3DS screen runs at 60, so every game ran 0.46 % fast. A frame of that
is invisible; a game that saves state, or that syncs to its music, is
not, and the error never goes away because it is built into the pacing.

**The sound was restarted every eighth of a second.** The port filled a
buffer with eight frames of samples and handed the whole buffer to the
sound service, restarting the service each time. A restart cuts the sound
off at the point it had reached and starts again, so the audio was
chopped into eighths of a second, and the service was asked for a rate no
hardware has.

## What the port does now

### The renderer draws into a picture, and the picture is scaled

`platform/3ds/gbgfx.cpp` no longer touches the framebuffer at all while
the emulator is running.

While the emulator runs, `drawBackground()` and `drawSprite()` fill in
four small per-scanline buffers of palette indices, and
`compositeScanline()` reduces them to the 160 pixels that are visible.
Nothing here allocates, and nothing here writes to the framebuffer.

Once the frame is finished, `gfxBlitGameFrame()` puts the finished
160x144 picture into the framebuffer in 8x8 blocks, so that the
transposed framebuffer is written a cache line at a time instead of a
pixel at a time.

The picture is put on the screen at its own size, in the middle of it, the
way the original port did it. A picture made of squares that are not
square on the screen is not the picture the Game Boy drew, and the scaled
picture is there for the screens where a hundred and sixty pixels across
is too small to be worth looking at: the Display menu has a setting for
it, and the scaled picture is still drawn the same way, in blocks.

The index is not a colour. It is a number from 0 to 63 that says which of
the sixty-four colours in the table the pixel is, and the table is rebuilt
only when a palette register changes. The old renderer kept a pointer to
a pointer to a colour for each of those sixty-four entries and followed
it for every pixel it drew.

The picture is the same picture. `tools/host/check_render.cpp` runs the
old renderer and the new one over the same randomised Game Boy states and
compares every pixel:

```
PASS: 2000 frames, 23040 pixels each, all identical.
```

The old renderer had bugs - it drew DMG sprites in the wrong order, it
split the screen incorrectly when the window started, and it showed stale
pixels when the background was switched off. They are still there. Fixing
them is an accuracy change, not a performance change, and it needs a
screen to look at.

### The frame has its own clock

`platform/3ds/frameclock.h` keeps the port's own time base. It does not
ask how long the last frame took and it does not add the frame time to a
running total: it knows how many frames it has presented and works out
the deadline of frame N from the epoch.

The unit is a tick of the console's clock, which runs at 268111856 Hz. One
Game Boy frame is 4488918.06 ticks, which is not a whole number, so the
deadline is kept as the rational 73546433499/16384 ticks a frame. The
division truncates, so a deadline is up to one tick - 3.7 nanoseconds -
early, and that error is bounded rather than accumulating: after ten hours
the frame that should be due at T is at most one tick early, exactly as
the first one was.

The wait is in two parts: most of it is handed to the kernel, and the
last 400 microseconds are spun. A sleep that overshoots is a frame that
starts late, and the spin keeps the deadline accurate without keeping the
processor busy for the whole frame.

`tools/host/check_timing.cpp` checks this with a clock the test drives:

```
A frame is 4488918.060 ticks, 16.742706 ms, 59.7275 frames a second.
PASS: the frame clock keeps its time: 64 ticks over 3600 frames with a
perfect clock, 160931 with sleeps that wake up a millisecond late.
```

The second number is the important one. A sleep that wakes up a
millisecond late, 3600 times over, would be 3.6 seconds late if the error
were added up. It is 0.6 milliseconds late, because the deadline is a
function of the frame number and not a running sum.

### The sound is played in blocks, and nothing is timed from the clock

`platform/3ds/audio.c` is a new transport, and it is not the design that
was borrowed from another emulator.

GameYob's own APU is unchanged. It is sampled every 84 Game Boy cycles,
which is 49932.57 Hz, and it produces 836 stereo frames a Game Boy frame.

The sound service plays one sample at a time on a channel, and a channel
is mono, so the two ears of the Game Boy's stereo output go to two
channels. Each channel is given a block of two frames of samples, and
when the block is full the service is told to play it once. The next
block is filled while that one is playing, and is handed over when it is
full.

The blocks follow one another without a gap: the sample after the last
one of a block is the first one of the next, because the emulator
produced them in that order. So the service plays the emulator's samples
in one piece, and there is no position anywhere that has to be worked out
from a clock. The earlier transport did exactly that: it kept a ring of
samples and worked out from the console's clock where the service had got
to. If the two clocks disagreed about the rate, the writer and the service
drifted apart, and the sound fell apart in a way that was very hard to
see. Nothing here is timed from the clock, so there is nothing to drift.

Two things the original port did are not done here, because they are why
the original port made no sound at all. It never told the service that
the console was making a sound: the command was written out and never
called, and the service will sit on a channel that is set up perfectly and
say nothing. And it played its samples on channels eight and nine, which
are the tone and noise channels; the channels below eight are the ones
that play recorded sound.

The samples are also made louder on the way. The APU's loudest sample is
about 1680, which is a twentieth of full scale, and at that level nothing
comes out of a 3DS's speaker. The gain is in the transport, not in the
APU: the APU computes exactly what it always computed, and the transport
makes it eight times louder on its way out.

The numbers are on the screen once a second, so that a dropout is not
invisible.

**What this is, and what it is not:** the transport is a routing and
timing layer. It moves samples from the APU to the speaker without losing
them. It is not a sample accurate emulation of the Game Boy's sound
hardware - the APU that produces the samples is GameYob's, unchanged, and
the part that is played is still a simplification of the real thing.

## What was measured

On a development machine, with the two renderers over the same randomised
states (`make -C tools/host bench`):

| | |
|---|---|
| Old renderer | 0.1437 ms a frame |
| New renderer | 0.0695 ms a frame |
| New renderer and the blit | 0.1727 ms a frame |
| The blit on its own | 0.1019 ms a frame |

Read that with care, because it is the measurement that looks worst for
the new code and it is the one that says least.

A development machine has a cache big enough to hide the problem. The old
renderer's 23040 scattered writes are cache misses on a console and cache
hits here. The blit writes 155520 bytes a frame in 240 contiguous runs,
which is 2400 cache lines; the old renderer touched 23040 of them. On this
machine the two are within a factor of two of each other; on a console,
where the framebuffer does not fit in the cache, they are not.

So the honest summary of the measurement is: **on the machine this was
measured on, the drawing itself is twice as fast, and the scaling that
replaces the scattered writes costs about as much again, so the two come
out about level. The claim that the new path is faster rests on the
cache behaviour, which is the reason for the change, and on the console's
much smaller cache, which has not been measured.**

What has not been measured:

- how long a frame takes on a console or in an emulator;
- how much of the frame the emulator's core takes, which is the same code
  as the DS build's and is not what this work was about;
- whether the scaling is the right amount of work for the console's
  memory system, which is the first thing to look at if the port is still
  slow.

## What to do if it is still slow

The order to try, and why:

1. **Measure it on the console first.** Everything below is a guess about
   a machine nobody has run this on. The emulator's core is the same code
   as the DS build's, so if the port is slower than the DS build, the
   difference is in the renderer, the blit, the pacing or the sound.
2. **The blit.** It writes 155520 bytes a frame. If the console's memory
   system cannot take that in the time available, the picture can be
   scaled less (one to one instead of one and a half), or drawn a row at a
   time into a framebuffer that is not turned on its side, at the cost of
   the cache behaviour that was the point of the change.
3. **The frame clock's spin.** 400 microseconds of spinning a frame is
   2.4 % of the processor, which is nothing, but it is a guess: the
   kernel's sleep granularity is not known from here, and the spin exists
   because of that.
4. **The sound transport's cache flushing.** The samples are flushed to
   the sound service when a block is full, which is 1672 frames' worth of
   writes every second and a half of game time.

## The tests

| Test | What it checks |
|---|---|
| `tools/host/check_render.cpp` | The new renderer draws the same picture as the old one, pixel for pixel, over randomised Game Boy states. |
| `tools/host/check_blit.cpp` | The picture lands in the right place at every scale, nothing is painted outside it, and a picture hanging off the edge is cut off rather than folded back. |
| `tools/host/check_timing.cpp` | The frame clock keeps its time, and a late sleep is not added up over thousands of frames. |
| `tools/host/check_audio.cpp` | The sound transport tells the service that the console is making a sound, hands it the emulator's samples in order, makes them louder, and turns them down again when it is muted. |

The old renderer is kept in `tools/host/reference_gbgfx.cpp` for the
renderer test to compare against. It is upstream GameYob's renderer, and
GameYob is MIT licensed.
