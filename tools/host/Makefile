# The host tests.
#
# The 3DS port's own files are compiled against a stand-in for the 3DS SDK
# (3ds.h) so that the renderer, the blit and the frame clock can be run
# and checked on a development machine. See check_render.cpp for what the
# tests are for.
#
#   make            build everything
#   make check      build and run the tests
#   make bench      build and run the benchmark

CXX      ?= g++
CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -Wno-unused-parameter
CXXFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu++11

# Keep track of the headers, so that a change to the stand-in for the SDK
# rebuilds everything that uses it.
DEPFLAGS := -MMD -MP

HERE     := $(CURDIR)
PLATFORM := $(HERE)/../../platform/3ds

# The stub directory comes first, so that the tests get the small versions
# of gameboy.h and menu.h rather than the real ones, which need the whole
# emulator. The common directory is last, for the headers that are shared
# and harmless.
INCLUDES := -I$(HERE) -I$(HERE)/stub -I$(PLATFORM)/include -I$(PLATFORM) \
            -I$(HERE)/../../platform/common/include

# The port is built with these, and they are not decoration: the sound
# engine's header keeps part of the class behind _3DS, so a file that
# does not define it sees a different class layout from the file that
# does. That is not a warning, it is two different types with the same
# name, and the tests would be looking at the wrong memory.
DEFINES  := -D_3DS -DARM11

BUILD    := build

SHIM     := $(HERE)/3ds_shim.c
STUBS    := $(HERE)/stubs.cpp
RENDERER := $(PLATFORM)/gbgfx.cpp
GFX      := $(PLATFORM)/3dsgfx.cpp
REFERENCE:= $(HERE)/reference_gbgfx.cpp

TESTS    := check_render check_blit check_timing check_audio check_apu debug_render
BENCHES  := bench_render

.PHONY: all check bench clean

all: $(TESTS) $(BENCHES)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

$(BUILD)/platform_%.o: $(PLATFORM)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

$(BUILD)/reference_gbgfx.o: $(REFERENCE) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# The APU. The port builds it with _3DS and ARM11 defined, and the sound
# engine's header keeps part of itself behind _3DS, so the test has to
# define them too or it is not the same file.
$(BUILD)/platform_soundengine.o: $(PLATFORM)/soundengine.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

$(BUILD)/stubs.o: $(STUBS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# The sound transport is C.
$(BUILD)/audio.o: $(PLATFORM)/audio.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# The renderer under test, and the renderer it replaced.
RENDER_OBJS := $(BUILD)/platform_gbgfx.o $(BUILD)/platform_3dsgfx.o $(BUILD)/reference_gbgfx.o

check_render: $(BUILD)/check_render.o $(RENDER_OBJS) $(BUILD)/3ds_shim.o $(BUILD)/stubs.o $(BUILD)/audio.o
	$(CXX) $^ -o $@

check_blit: $(BUILD)/check_blit.o $(RENDER_OBJS) $(BUILD)/3ds_shim.o $(BUILD)/stubs.o $(BUILD)/audio.o
	$(CXX) $^ -o $@

debug_render: $(BUILD)/debug_render.o $(RENDER_OBJS) $(BUILD)/3ds_shim.o $(BUILD)/stubs.o $(BUILD)/audio.o
	$(CXX) $^ -o $@

check_audio: $(BUILD)/check_audio.o $(BUILD)/audio.o $(BUILD)/3ds_shim.o $(BUILD)/stubs.o
	$(CXX) $^ -o $@

# The APU, and the samples it produces. The sound engine is the port's own
# file, so it is compiled here like any other.
check_apu: $(BUILD)/check_apu.o $(BUILD)/platform_soundengine.o $(BUILD)/audio.o $(BUILD)/3ds_shim.o $(BUILD)/stubs.o
	$(CXX) $^ -o $@

check_timing: $(BUILD)/check_timing.o $(BUILD)/platform_gbgfx.o $(BUILD)/platform_3dsgfx.o $(BUILD)/3ds_shim.o $(BUILD)/stubs.o $(BUILD)/audio.o
	$(CXX) $^ -o $@

bench_render: $(BUILD)/bench_render.o $(RENDER_OBJS) $(BUILD)/3ds_shim.o $(BUILD)/stubs.o $(BUILD)/audio.o
	$(CXX) $^ -o $@

check: $(TESTS)
	@echo
	@./check_render
	@echo
	@./check_blit
	@echo
	@./check_timing
	@echo
	@./check_audio
	@echo
	@./check_apu

bench: $(BENCHES)
	@./bench_render

clean:
	rm -rf $(BUILD) $(TESTS) $(BENCHES)

-include $(wildcard $(BUILD)/*.d)
