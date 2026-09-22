# whisperpipe

Real-time local speech-to-text for Linux. Listens to your system audio sink
(monitor source) via the PulseAudio/PipeWire API, chunks the audio, and transcribes
it with [whisper.cpp](https://github.com/ggml-org/whisper.cpp) — fully local,
with automatic language detection and timestamped output.

```
[21:14:03.012] [en] and so the whole pipeline is working now
[21:14:06.530] [en] I will close the issue tomorrow
```

## Layout

```
whisperpipe/
├── whisperpipev2.c      # the app (single C file)
├── Makefile             # builds whisper.cpp + links the app
├── download-model.sh    # fetches a GGML model into ./models/
├── models/              # (created by download-model.sh; gitignored)
└── whisper.cpp/         # whisper.cpp — git submodule, ships with this repo
    └── build/           # (created by `make`; gitignored)
```

`whisper.cpp` is a pinned **git submodule** sitting next to the app. When you
clone this repo (with `--recursive`) it arrives already checked out — no manual
download required. The Makefile builds it in place and links the app against it.

## Requirements

- Linux (any distribution) with a C toolchain: `cc`/`gcc`, `make`, `cmake`, `git`
- **PulseAudio client libraries** — the app records through the Pulse-compatible
  API, which works with both classic PulseAudio and **PipeWire** (with
  `pipewire-pulse` running).

Per-distro packages:

| Distro                 | Packages                                                                                          |
| ---------------------- | --------------------------------------------------------------------------------------------------- |
| Arch / CachyOS         | `base-devel cmake pulseaudio`                                                                       |
| Debian / Ubuntu        | `build-essential cmake libpulse-dev`                                                                |
| Fedora / RHEL          | `gcc make cmake pulseaudio-libs-devel`                                                              |

If you run PipeWire (most current desktops), make sure `pipewire-pulse` is
active: `pw-cli -n 2 info | grep -i pulse` or check that your desktop is
routing audio through PipeWire.

### GPU backend (optional, recommended)

CPU works out of the box. For faster inference you can build whisper.cpp with a
GPU backend:

- **Vulkan** — works on AMD (e.g. RX 9070 XT), Intel and NVIDIA:
  install your driver's Vulkan ICD (AMD: `mesa-vulkan-drivers`, NVIDIA:
  `libgl1-vulkan0` / the `nvidia` driver package).
- **CUDA** — NVIDIA: `nvidia-cuda-toolkit`
- **HIP** — AMD with ROCm: `hip`

## Install & Build

```sh
# 1. clone (recursive so whisper.cpp comes along)
git clone --recursive https://github.com/<you>/whisperpipe
cd whisperpipe

# (if you cloned without --recursive)
git submodule update --init --recursive

# 2. build whisper.cpp + the app (CPU backend)
make

# — or, with a GPU backend —
make GPU=Vulkan    # AMD/Intel/NVIDIA via Vulkan
make GPU=CUDA      # NVIDIA
make GPU=HIP       # AMD ROCm
```

`make` configures and builds `whisper.cpp/` on the first run (a few minutes),
then links `./whisperpipe`. Rebuilding whisper.cpp only happens when its
`CMakeLists.txt` changes.

```sh
make clean       # remove ./whisperpipe
make distclean   # also remove whisper.cpp/build (full reconfigure next build)
```

### Get a model

```sh
./download-model.sh              # ggml-large-v3-turbo (default — good speed/quality)
./download-model.sh small        # faster, smaller
./download-model.sh base         # quickest
./download-model.sh large-v3     # highest quality, slower
```

This downloads the model into `models/` next to the binary — exactly where the
app looks by default, so no flags are needed afterwards. Any other
[GGML model file](https://huggingface.co/ggerganov/whisper.cpp) works too;
pass it with `-m`.

## Usage

```sh
./whisperpipe [options]
```

| Flag             | Meaning                                        | Default                                             |
| ---------------- | ---------------------------------------------- | --------------------------------------------------- |
| `-m, --model`    | Path to a GGML model file                      | `models/ggml-large-v3-turbo.bin` (next to the binary) |
| `-s, --source`   | PipeWire/Pulse **monitor source** to listen on | `alsa_output.pci-0000_09_00.4.analog-stereo.monitor` |
| `-b, --backend`  | Directory of ggml backend `.so` files          | `build/bin` (relative to where whisper.cpp was built) |
| `-t, --threads`  | Inference threads                              | `6` (match your physical core count)                 |
| `-r, --rms`      | RMS below this is treated as silence and skipped | `100`                                          |
| `-c, --chunk`    | Audio chunk length in seconds (1–10)           | `3`                                                  |
| `-h, --help`     | Help                                           |                                                      |

### Finding your monitor source

The one thing that differs per machine: the **monitor source name**. It
follows the pattern `<your-output-device>.monitor`. List available ones:

```sh
pactl list sources | grep -A1 'Monitor of'
# or with PipeWire:
pw-cli -n 2 list-objects source | grep -E 'name:|monitor'
```

Example:

```
$ pactl list sources | grep 'Monitor of'
alsa_output.pci-0000_09_00.4.analog-stereo.monitor
```

Then run:

```sh
./whisperpipe -s alsa_output.pci-0000_09_00.4.analog-stereo.monitor
```

The default `-s` value is just one machine's default — override it on any
other system (it's a flag, not a compile-time constant).

### How it works

- Records 16 kHz mono PCM in chunks (`-c`, default 3 s) from the monitor
  source — i.e. whatever is playing through that output.
- Chunks quieter than the RMS gate (`-r`) are skipped.
- Each remaining chunk is transcribed by whisper.cpp with **automatic language
  detection** (`language = auto`).
- Segments are printed to stdout with wall-clock time, millisecond offset,
  detected language, and text:

  ```
  [21:14:03.012] [en] and so the whole pipeline is working now
  ```

  (A temporary notes file is written while running and erased on exit — the
  transcript is on your terminal / whatever you redirect stdout to.)

```sh
# save a transcript
./whisperpipe -s <your.monitor> 2>&1 | tee transcript.log
```

Stop with `Ctrl+C`.

## Troubleshooting

- **`pa_simple_new failed`** — the Pulse/PipeWire audio stack isn't running, or
  the source name is wrong. Check `pactl list sources`, and on PipeWire
  systems that `pipewire-pulse` is active.
- **`could not load model`** — run `./download-model.sh` or pass `-m` with a
  valid path.
- **`cannot open shared object file: libwhisper.so`** — whisper.cpp isn't
  built or the build dir moved; run `make` again (or `make distclean && make`).
- **No text even though audio plays** — raise the chunk size (`-c 5`), lower
  the RMS gate (`-r 50`), and make sure you're monitoring the output device
  audio is actually going through.
- **Slow on CPU** — try a smaller model (`./download-model.sh base`) or build
  with `make GPU=Vulkan`.

## Building whisper.cpp manually

If you prefer to control the whisper.cpp build yourself:

```sh
cmake -B whisper.cpp/build -S whisper.cpp \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
  -DGGML_VULKAN=ON -DGGML_CPU=ON \
  -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BUILD_TESTS=OFF
cmake --build whisper.cpp/build -j$(nproc)
```

then `make` (the Makefile detects the existing build and only links the app).

## License

MIT — see the whisper.cpp submodule for its own MIT license.
