# This port against the original

The original is Stewmath's GameYob, master branch, and the part that matters
here is its 3DS port: `platform/3ds` and the shared `platform/common` code
it sits on. This is a file-by-file comparison of that port against this
one, and a list of what was put back, what was deliberately left alone, and
why.

## How the comparison was made

Two ways, and they answer two different questions.

**Reading.** Every file in the original's `platform/3ds` and
`platform/common` was diffed against the same file here, line by line. The
emulation core (`gbcpu.cpp`, `mmu.cpp`, `sgb.cpp`, `romfile.cpp`, `io.cpp`,
`gbs.cpp`, `debugger.cpp`, `mbc.cpp`, `gbprinter.cpp`, `version.cpp`) came
out **byte for byte identical**. The port has not touched the emulator.

**Running.** The original's own 3DS renderer is kept in the host harness as
`tools/host/reference_gbgfx.cpp`, and the harness renders the same frames
with the original's code and with this port's code and compares the pixels.
Two hundred frames of a test ROM: **all 23,040 pixels of every frame
identical**. The picture this port puts on the screen is the original's
picture, pixel for pixel.

So: where this port and the original differ in what the user sees, it is
not because the picture is drawn differently. It is because of the way the
picture is *put on the screen*, and the way the *sound is played*.

## The file-by-file comparison

| Original file | This port | Verdict |
|---|---|---|
| `platform/3ds/gbcpu.cpp`, `mmu.cpp`, `sgb.cpp`, `romfile.cpp`, `io.cpp`, `gbs.cpp`, `debugger.cpp`, `mbc.cpp`, `gbprinter.cpp`, `version.cpp` | identical | **Identical.** The emulator has not been touched. |
| `platform/3ds/gbgfx.cpp` | rewritten to draw into a colour-indexed picture that is blitted in 8x8 blocks | **Equivalent pixels, twice as fast.** The harness compares against the original's renderer: 23,040 pixels a frame, all identical. See below for why the original's renderer was not put back. |
| `platform/3ds/3dsgfx.cpp` | rewritten on libctru's framebuffer, 16 bits a pixel | **Equivalent, and the picture is in the right place.** The original drew into its own 24-bit framebuffer with its own active/inactive pair and its own swap; this one draws into the SDK's back buffer and swaps once. Same geometry: the original's picture was 1:1 and centred, and so is this one's (Display -> Picture Size -> Original). |
| `platform/3ds/soundengine.cpp` | the original's APU, with the two terminals on separate ears instead of mixed to mono | **Kept, with one improvement.** The APU itself is the original's, untouched. The original mixed both terminals into one mono channel; this one puts the first terminal on the left ear and the second on the right, which is what the Game Boy Color's two terminals actually are. |
| `platform/3ds/soundengine.cpp` (the CSND part) | rewritten, in `audio.c` | **Restored this round.** The original's scheme was two buffers, one played while the other was filled. This port's rewrite had one buffer per ear and went on writing to it while the service was reading it. See below. |
| `platform/3ds/main.cpp` | modern libctru, 16-bit screens, the diagnostics and the status strip | **Equivalent and extended.** The original used `gfxInit()`, `hidInit(NULL)` and `csnd_init()`; this one uses the current library. Nothing was dropped. |
| `platform/3ds/inputhelper.cpp` | `aptMainLoop()` and `aptShouldClose()` instead of the original's `aptGetStatus()` loop | **Equivalent.** The original's loop called `aptReturnToMenu()` and `aptSignalReadyForSleep()` by hand; libctru's `aptMainLoop()` does the same two things. `aptShouldClose()` is a flag test, not a wait, so calling it every frame costs nothing. |
| `platform/3ds/console.cpp` | the real console instead of the original's `return NULL` | **Improvement.** The original's console printed nothing at all. This one prints, in colour, and can be put away while the graphical interface is up. |
| `platform/3ds/printconsole.cpp` | libctru's `PrintConsole` instead of the original's hand-drawn one | **Improvement.** The original drew the text into the framebuffer a pixel at a time. |
| `platform/3ds/3dio.cpp` | the current libctru file system | **Equivalent.** The original was written against libctru v0.6 (`FS_makePath`, `PATH_CHAR`, `FS_archive`); the current library has `fsMakePath`, `FS_Path` and a `u64` archive handle. The interface is the same. |
| `platform/3ds/nifi.cpp` | the same empty stubs, but they return `false` instead of nothing | **Equivalent.** The original's stubs returned an undefined value; these return `false`. |
| `platform/3ds/3dtimer.cpp` | identical | **Identical.** |
| `platform/common/menu.cpp` | 47 items against the original's 43 | **Nothing lost.** The original's 43 are here, in the same order, with the same choices. Four were added: Status Strip, Picture Size, Diagnostics, Sound Test. |
| `platform/common/cheats.cpp`, `config.cpp`, `error.cpp`, `filechooser.cpp`, `gameboy.cpp`, `gbmanager.cpp` | the graphical interface instead of the text console | **Nothing lost, different screens.** The cheats, key mapping, ROM information, error and statistics screens are drawn instead of printed. |

## The audit

Two checks were run over the two ports, so that "nothing was lost" is a
measurement rather than a reading.

**The menu.** Every item was extracted from both `platform/common/menu.cpp`
files, in order, with the choices each one offers. The original has 43
items and this port has 47. The sequence is the original's, unchanged,
with four items put into it:

    Exit, Reset, State Slot, Save State, Load State, Delete State,
    Quit to Launcher, Exit without saving, Suspend, Button Mapping,
    Manage Cheats, Rumble Pak, GB Camera, Console Output, GB Printer,
    Autosaving, Save Settings, Game Screen, Single Screen, Scaling,
    Scale Filter, SGB Borders, Custom Border, Select Border,
    **Status Strip**, **Picture Size**, GBC Bios, Detect GBA, GBC Mode,
    SGB Mode, Wait for Vblank, Hblank, Window, Sound, Sound Timing Fix,
    ROM Info, Version Info, **Diagnostics**, **Sound Test**, Channel 1,
    Channel 2, Channel 3, Channel 4, Link to DS, Stub, Local Link,
    Swap Focus

Nothing was removed, and no item offers different choices than it did.
("Scaling" and "Scale Filter" are the DS's own items and did nothing on
the 3DS in the original either; "Picture Size" is the 3DS's version of
them.)

**The functions.** Every function in the original's `platform/3ds` and
`platform/common` was extracted and looked for here. Nine are not here:

| Original's | Why |
|---|---|
| `getPixel`, `gfxGetActiveFramebuffer`, `gfxGetInactiveFramebuffer`, `gfxMySwapBuffers` | the original's own framebuffer handling; libctru's framebuffer and swap are used instead |
| `csnd_init`, `initSampler`, `addSample`, `swapBuffers` | the original's sound service handling; `platform/3ds/audio.c` does it instead, and `swapBuffers` is the two-buffer scheme that was put back |
| `CSND_setchannel_enabled` | the original's hand-written service command; the library's own is used |

Nothing that the user could reach is among them.

## What was put back, and why

### The sound transport: two buffers per ear

This is the one that matters, and it was found by reading the original's
sound code next to this port's.

The original kept **two** buffers and alternated between them: the service
played one while the other was filled, and when the one being filled was
full it was handed over and the one that had been playing was filled
instead. The buffer the service was reading was never the buffer being
written to.

This port's rewrite kept **one** buffer per ear. It filled the buffer,
handed it to the service, and then went on filling the same buffer with
the next frame's samples **while the service was still reading it**.

That is a race, and it is the reason the sound could not be right:

* the first 33 ms of every block was the emulator's samples;
* everything after it was whatever the service happened to read while the
  emulator was writing over it.

The original's design has no such window, so it was put back. Each ear now
has two buffers and the buffers take turns, exactly as in the original.

Two details came with it:

* **Each buffer has a channel of its own**, so the address the service is
  given for a buffer never changes, and the channel that is handed a
  buffer is the one that played it a block ago and has been idle since.
* **The channels are asked for, not assumed.** The service decides which
  channels an application may use, so the transport takes them from the
  mask the service hands out rather than picking numbers. Four are used
  when four are available: two for each ear.

The gain of eight times and the block size of two frames stayed as they
were. The APU is still the original's, byte for byte.

### The command that starts a channel

The original never sent the command that says "this channel plays" for the
channels it set up: it wrote the function out and never called it. libctru's
own `csndPlaySound` sends it, so it is sent here too, once per channel
after the setup. On its own it is harmless; if the service was waiting for
it, it is the difference between a channel that is set up and a channel that
is playing.

### The picture is 1:1 and centred

Already the case (Display -> Picture Size -> Original): the original drew
the picture at 160x144 in the middle of the top screen, and so does this
one. The scaled option is there for the screens that are taller than they
are wide.

## What was deliberately not put back

**The original's renderer.** It draws every pixel one at a time into a
24-bit framebuffer. This port draws the picture into a colour-indexed
buffer and blits it in 8x8 blocks. The two produce **the same pixels** -
the harness checks this frame by frame - and the block version is about
twice as fast. Putting the original's renderer back would change nothing
the user can see and would cost half the frame time. It was left alone.

**The original's 24-bit framebuffer.** The screens are 16 bits a pixel
here. The bus carries a third less for the picture, and the only visible
difference is a slightly coarser gradient, which on a Game Boy palette of
four shades per palette is not visible at all.

**The original's mono mix.** The original mixed both of the Game Boy
Color's terminals into one channel. This port puts them on separate ears.

**The original's eight-frame blocks.** They are 134 ms of delay between the
game and the speaker: the sound would be a seventh of a second behind the
picture. Two frames is 33 ms, which is short enough that the sound is with
the picture and long enough that the service is not restarted every frame.

**The original's channels eight and nine.** Those are the tone and noise
channels; the samples would have been played by the wrong part of the sound
hardware. The recorded-sound channels are the ones below eight.

**The original's text interface.** The graphical interface was asked for
and it stays. Nothing was lost in the change: the original's 43 menu items
are all still there.

## What the comparison found

One real bug, in this port's own code:

* **The sound transport wrote over the samples the service was reading.**
  Fixed by putting the original's two-buffer scheme back, as described
  above.

And one stale comment:

* `packColour()` in `platform/3ds/gbgfx.cpp` described the framebuffer as
  holding three bytes a pixel. It holds two, since the screens were moved
  to 16 bits a pixel. The comment now says so.

Everything else the comparison turned up was the original being written
against libctru v0.6 and this port being written against v2.7.0: the file
system, the console, the framebuffer, the program-switch handling. None of
those differences cost a feature.

## What has not been checked on a console

Nothing in this port has been run on real hardware by the person who wrote
it. The host harness checks the renderer against the original's own
renderer, the picture's geometry, the frame clock's arithmetic and the
sound transport's handover to the service; it cannot check what the
speaker does with the samples. The sound test (Debug -> Sound Test) is
what does that, and the diagnostics panel (Debug -> Diagnostics) says
whether the service thinks the channels are playing.

The panel also says how many times the service **refused** the commands
the transport gave it. That is the difference between "there is no sound
because the game is not making any" and "there is no sound because the
service would not play it", which nothing else on the screen can tell.
