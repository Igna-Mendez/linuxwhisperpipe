# whisperpipe — real-time local STT (PipeWire + whisper.cpp)
#
#   make                 # build whisper.cpp (CPU) + link the app -> ./whisperpipe
#   make clean           # remove ./whisperpipe
#   make distclean       # also remove the whisper.cpp build tree
#
# GPU backends (optional):
#   make GPU=Vulkan      # AMD/Intel/NVIDIA
#   make GPU=CUDA        # NVIDIA
#   make GPU=HIP         # AMD (ROCm)
#   make GPU=CPU         # default

WHISPER_DIR := whisper.cpp
BUILD_DIR   := $(WHISPER_DIR)/build

CC      ?= cc
CFLAGS  = -O2 -Wall -fopenmp -I$(WHISPER_DIR)/include -I$(WHISPER_DIR)/ggml/include
# -fopenmp is required: the statically-linked ggml archives are built with
# GGML_OPENMP=ON (the CMake default) and reference the GOMP_* runtime symbols.

# Backend selection: the matching GGML_<backend>=ON CMake flag and link entry
# are added automatically. CPU is always linked (fallback + required).
GPU ?= CPU

# Static archives (BUILD_SHARED_LIBS=OFF) make the binary fully self-contained:
# no LD_LIBRARY_PATH, no ldconfig, no RUNPATH. Link order matters: consumers
# first, providers after (classic static-archive rule).
WHISPER_LIB      := $(BUILD_DIR)/src/libwhisper.a
GGML_LIB         := $(BUILD_DIR)/ggml/src/libggml.a
GGML_CPU_LIB     := $(BUILD_DIR)/ggml/src/libggml-cpu.a
GGML_BASE_LIB    := $(BUILD_DIR)/ggml/src/libggml-base.a
LDFLAGS =
ifneq ($(GPU),CPU)
  GGML_GPU_LIB := $(BUILD_DIR)/ggml/src/libggml-$(shell echo $(GPU) | tr 'A-Z' 'a-z').a
  LDLIBS  = $(WHISPER_LIB) $(GGML_LIB) $(GGML_GPU_LIB) $(GGML_CPU_LIB) $(GGML_BASE_LIB) \
            -lpulse-simple -lpulse -ldl -lm -lpthread -lstdc++
else
  LDLIBS  = $(WHISPER_LIB) $(GGML_LIB) $(GGML_CPU_LIB) $(GGML_BASE_LIB) \
            -lpulse-simple -lpulse -ldl -lm -lpthread -lstdc++
endif

TARGET  := whisperpipe
SRC     := whisperpipev2.c
ALL_LIBS = $(WHISPER_LIB) $(GGML_LIB) $(GGML_CPU_LIB) $(GGML_BASE_LIB)
ifneq ($(GPU),CPU)
ALL_LIBS += $(GGML_GPU_LIB)
endif

# Per-GPU config stamp: re-runs the build when GPU= changes, so the app relinks.
$(TARGET): $(SRC) | $(ALL_LIBS) $(BUILD_DIR)/.config_$(GPU)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

# cmake (not make) produces the libraries; defer to the config stamp so make
# doesn't die with "No rule to make target ... libwhisper.a" on a fresh clone.
$(ALL_LIBS): $(BUILD_DIR)/.config_$(GPU)
	@test -e $@ || $(MAKE) $(BUILD_DIR)/.config_$(GPU)

# Reconfigure when the GPU backend changes — otherwise the CMake cache keeps
# the previous backend's flags (silent wrong-backend build).
$(BUILD_DIR)/.config_$(GPU): $(WHISPER_DIR)/CMakeLists.txt
	mkdir -p $(BUILD_DIR)
	@if [ "$$(cat $(BUILD_DIR)/.gpu_backend 2>/dev/null)" != "$(GPU)" ]; then \
	    rm -f $(BUILD_DIR)/CMakeCache.txt $(BUILD_DIR)/.config_*; \
	    printf '%s' "$(GPU)" > $(BUILD_DIR)/.gpu_backend; \
	fi
	cmake -B $(BUILD_DIR) -S $(WHISPER_DIR) \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DBUILD_SHARED_LIBS=OFF \
	  -DGGML_VULKAN=$(if $(filter Vulkan,$(GPU)),ON,OFF) \
	  -DGGML_CUDA=$(if $(filter CUDA,$(GPU)),ON,OFF) \
	  -DGGML_HIP=$(if $(filter HIP,$(GPU)),ON,OFF) \
	  -DGGML_CPU=ON \
	  -DGGML_NATIVE=ON \
	  -DWHISPER_BUILD_EXAMPLES=OFF \
	  -DWHISPER_BUILD_TESTS=OFF
	cmake --build $(BUILD_DIR) -j$(shell nproc 2>/dev/null || echo 2)
	@for f in $(ALL_LIBS); do \
	  test -e "$$f" || { echo "expected $$f but cmake did not produce it" >&2; exit 1; }; \
	done
	touch $@

.PHONY: all clean distclean
all: $(TARGET)

clean:
	rm -f $(TARGET)

distclean: clean
	rm -rf $(BUILD_DIR)
