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

BUILD    := build

SHIM     := $(HERE)/3ds_shim.c
STUBS    := $(HERE)/stubs.cpp
RENDERER := $(PLATFORM)/gbgfx.cpp
GFX      := $(PLATFORM)/3dsgfx.cpp
REFERENCE:= $(HERE)/reference_gbgfx.cpp

TESTS    := check_render check_blit check_timing check_audio debug_render
BENCHES  := bench_render

.PHONY: all check bench clean

all: $(TESTS) $(BENCHES)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/platform_%.o: $(PLATFORM)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/reference_gbgfx.o: $(REFERENCE) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/stubs.o: $(STUBS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

# The sound transport is C.
$(BUILD)/audio.o: $(PLATFORM)/audio.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

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

bench: $(BENCHES)
	@./bench_render

clean:
	rm -rf $(BUILD) $(TESTS) $(BENCHES)

-include $(wildcard $(BUILD)/*.d)
