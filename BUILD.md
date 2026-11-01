# Building GameYob

GameYob is built for the Nintendo DS, for the Nintendo 3DS and for a
desktop. This file is about the 3DS, because that is the one that is not
obvious, and about the checks that can be run without any of them.

## The 3DS port

The port needs devkitARM, libctru and 3dsxtool. The easiest way to get
them is devkitPro's own container, which has all three in it:

```sh
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkitarm:latest \
    make -C platform/3ds
```

`docker/ci-build.sh` does that, and also builds `makerom`, which the
`.cia` and the `.3ds` need and which is neither in the container nor in
devkitPro's package manager:

```sh
bash docker/ci-build.sh
```

If you would rather build it yourself, install the tools with devkitPro's
package manager:

```sh
export DEVKITPRO=/opt/devkitpro
curl -fsSL https://apt.devkitpro.org/install-devkitpro-pacman | sudo bash
sudo dkp-pacman -S 3ds-dev 3dsxtool
```

and build `makerom` from its source:

```sh
git clone https://github.com/3DSGuy/Project_CTR.git tools/makerom
make -C tools/makerom deps
make -C tools/makerom
```

Then:

```sh
make -C platform/3ds          # the .3dsx
make -C platform/3ds cia      # the .cia
make -C platform/3ds 3ds      # the .3ds
```

The `.3dsx` is the one the Homebrew Launcher runs. The `.cia` is installed
by a title manager. The `.3ds` is a card image.

The build recurses into `platform/3ds/build/`, and the recursion is where
the parallelism belongs: `make -j8` at the top level would start the
`.3dsx`, the `.cia` and the `.3ds` rules at the same time, and they all
write to the same directory.

## The interface

The 3DS port draws its own interface: the menu and the file browser are
panels of colour and text, not pages of console output. The drawing kit is
`platform/3ds/uigfx.cpp` (the rasteriser), `platform/3ds/uifont.cpp` (the
fonts) and `platform/3ds/uiscreens.cpp` (the panels), and
`platform/3ds/ui3ds.cpp` puts GameYob's own menu and file list on the
screen with them. The text console is still there for what it is good at -
the frame rate, the log and the menus that are drawn as text - but it is
put out of the way while the panels are up.

The kit, the fonts and the look of the panels come from
[GBabe](https://github.com/Planta-dc/3ds-GBabe), the other Game Boy
emulator for the 3DS, which is MIT licensed; the licences are in
`platform/3ds/third_party/`. Only the look was taken. The code that draws
the menu is this port's own, and GBabe's pictures and sounds were not
brought over.

The screens are 16 bits a pixel (RGB565) rather than 24, so a panel can be
copied into the framebuffer without being converted, and the game's
picture takes a third less memory and a third less of the bus.

## The host tests

The port's own files - the renderer, the blit, the frame clock and the
sound transport - are written so that they can be run on a development
machine, against a stand-in for the 3DS SDK in `tools/host`. Nothing about
the emulator is changed to make this work; it is the same code.

```sh
make -C tools/host            # build the tests
make -C tools/host check      # run them
make -C tools/host bench      # measure the renderer
```

What the tests are for:

| Test            | What it checks                                              |
|-----------------|-------------------------------------------------------------|
| `check_render`  | The new renderer draws the same picture as the old one, pixel for pixel, over randomised Game Boy states. |
| `check_blit`    | The picture lands in the right place at every scale, and nothing is painted outside it. |
| `check_timing`  | The frame clock keeps its time, and a late sleep is not added up over thousands of frames. |
| `check_audio`   | The sound transport keeps its distance from the sound service, and says so when it cannot. |

The renderer test runs the old renderer and the new one over the same
randomised states and compares every pixel. The old renderer is upstream
GameYob's, kept in `tools/host/reference_gbgfx.cpp`; GameYob is MIT
licensed, so there is nothing wrong with using it to check the new work.

## Checking the port against the SDK

`tools/host/check-libctru.sh` reads every source file of the port with the
SDK's own headers and reports what does not fit. It does not link
anything:

```sh
tools/host/check-libctru.sh "$DEVKITPRO/libctru/include"
```

This is the check that catches an SDK that has moved on. The port was
written against libctru v0.6, whose file system and graphics interfaces no
longer exist; the current SDK has `fsMakePath` and `gfxGetFramebuffer`
instead, and the port was rewritten for them.

## What has and has not been shown to work

The port has been checked against the current SDK's headers, and the
renderer, the blit, the frame clock and the sound transport have been run
and checked on a development machine.

The continuous integration links it and keeps the result, so the port has
been linked; it has never been run on a console or on an emulator. Nothing
in this repository can show that, and `platform/3ds/3DS_PERFORMANCE.md`
says the same thing about the measurements in it.
