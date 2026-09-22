# ----------------------------------------------------------------------------
# whisperpipe — real-time local STT (PipeWire + whisper.cpp)
#
# Build layout (everything lives inside this repo):
#   whisperpipe/
#     whisperpipev2.c      <- this app
#     Makefile
#     whisper.cpp/         <- whisper.cpp checkout (git submodule)
#       build/             <- CMake build dir (created by `make whisper`)
#
# Usage:
#   make                 # build whisper.cpp (CPU) + link the app  -> ./whisperpipe
#   make clean           # remove ./whisperpipe
#   make distclean       # also remove the whisper.cpp build tree
#
# GPU backends (optional). Configure before building:
#   make GPU=Vulkan      # AMD/Intel/NVIDIA via Vulkan (works on RX 9070 XT)
#   make GPU=CUDA        # NVIDIA
#   make GPU=HIP         # AMD (ROCm)
#   make GPU=CPU         # default (any machine)
# ----------------------------------------------------------------------------

WHISPER_DIR := whisper.cpp
BUILD_DIR   := $(WHISPER_DIR)/build

CC      ?= cc
CFLAGS  = -O2 -Wall -I$(WHISPER_DIR)/include -I$(WHISPER_DIR)/ggml/include

# ---------------------------------------------------------------------------
# Backend selection. The matching GGML_<backend>=ON CMake flag and -lggml-<backend>
# link flag are added automatically. CPU is always linked (fallback + required).
# ---------------------------------------------------------------------------
GPU ?= CPU

ifneq ($(GPU),CPU)
  LDLIBS_GPU := -lggml-$(shell echo $(GPU) | tr 'A-Z' 'a-z')
endif

# Absolute rpath to the in-tree build dir (whisper.cpp is built inside this
# repo), so the app runs from anywhere after `make` without an LD_LIBRARY_PATH.
LDFLAGS = -L$(abspath $(BUILD_DIR)/bin) -Wl,-rpath,$(abspath $(BUILD_DIR)/bin)
LDLIBS  = -lwhisper -lggml -lggml-base -lggml-cpu $(LDLIBS_GPU) \
          -lpulse-simple -lpulse -lm -lpthread -lstdc++

TARGET  := whisperpipe
SRC     := whisperpipev2.c

# Make sure the whisper.cpp libs exist before linking the app.
$(TARGET): $(SRC) | $(BUILD_DIR)/bin/libwhisper.so
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

# Build whisper.cpp into the sibling directory (idempotent).
$(BUILD_DIR)/bin/libwhisper.so: $(WHISPER_DIR)/CMakeLists.txt
	cmake -B $(BUILD_DIR) -S $(WHISPER_DIR) \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DBUILD_SHARED_LIBS=ON \
	  -DGGML_VULKAN=$(if $(filter Vulkan,$(GPU)),ON,OFF) \
	  -DGGML_CUDA=$(if $(filter CUDA,$(GPU)),ON,OFF) \
	  -DGGML_HIP=$(if $(filter HIP,$(GPU)),ON,OFF) \
	  -DGGML_CPU=ON \
	  -DWHISPER_BUILD_EXAMPLES=OFF \
	  -DWHISPER_BUILD_TESTS=OFF
	cmake --build $(BUILD_DIR) -j$(shell nproc 2>/dev/null || echo 2)

.PHONY: all clean distclean
all: $(TARGET)

clean:
	rm -f $(TARGET)

distclean: clean
	rm -rf $(BUILD_DIR)
