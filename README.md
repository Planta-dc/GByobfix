## Info about GameYob Branches

The master branch of this repository (https://github.com/Drenn1/GameYob)
currently contains the source for an unfinished overhaul of GameYob after
release 0.5. In particular, this overhaul allows for the emulation of multiple
gameboys, in order to improve wireless link cable emulation by emulating both
gameboys on both systems. It also facilitated the 3DS port (and SDL for
testing purposes).

However, because speed suffered slightly and kinks were never fully worked out,
no public releases were made with this branch. Updates were instead done on the
"v0.5-hotfix" branch to fix issues with GameYob version 0.5.

Also, despite some residual 3DS code in this repository, SteveIce's fork for the
3DS version had far more work done on it. That repository is down now, but
a fork can be found here: https://github.com/ZopharsDomain/GAMEBOYC-3DS-GameYob

The 3DS version had its own kinks, which means you may still prefer to use the
DS version even on a 3DS. It is recommended to install it using the DSiWare CIA
file provided with the newer releases of GameYob DS.

## Features

- Gameboy, Gameboy Color, Super Gameboy emulation
- Supports Gameboy Color Bios ("boot rom") for custom palettes
- Auto SRAM saving
- Save States
- Cheat Codes
- Remappable controls
- Custom borders
- Scale to fill the screen
- Gameboy Printer emulation
- GBS music playback

## The 3DS port

The 3DS port in `platform/3ds` has been rewritten: the renderer draws into
a picture that is scaled onto the screen instead of drawing into the
framebuffer pixel by pixel, the port keeps its own frame clock instead of
following the display's vertical blank, and the sound is handed to the
sound service in blocks that follow one another, with nothing timed from
the clock. GameYob's own emulator and its own APU are unchanged.

`platform/3ds/3DS_VS_ORIGINAL.md` compares the port with the original
GameYob release file by file, and says what was put back and why.
`platform/3ds/3DS_PERFORMANCE.md` says what was measured and what was
not, and `BUILD.md` says how to build it and how to run the checks. The
checks are the interesting part: the renderer is compared with the
renderer it replaced, pixel for pixel, over randomised Game Boy states,
and the frame clock and the sound transport are checked against a clock
the test drives.

## More info

See the GBAtemp thread for more information:

http://gbatemp.net/threads/gameyob-a-gameboy-emulator-for-ds.343407/
