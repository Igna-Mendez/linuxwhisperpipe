# linuxwhisperpipe

Real-time local speech-to-text for Linux. Captures your system audio (the
monitor source of an output device) through the PipeWire/PulseAudio API,
gates out silence, and transcribes it with
[whisper.cpp](https://github.com/ggml-org/whisper.cpp) — fully local, automatic
language detection, timestamped output.

```
[21:14:03.012] [en] and so the whole pipeline is working now
[21:14:06.530] [en] I will close the issue tomorrow
```

## Requirements

- A C toolchain: `cc`/`gcc`, `make`, `cmake`, `git`
- PulseAudio client libraries (works with classic PulseAudio *and* PipeWire
  with `pipewire-pulse`, which most desktops use):
  - Arch / CachyOS: `sudo pacman -S base-devel cmake pulseaudio`
  - Debian / Ubuntu: `sudo apt install build-essential cmake libpulse-dev`
  - Fedora: `sudo dnf install gcc make cmake pulseaudio-libs-devel`

## Quick start

```sh
# 1. clone — whisper.cpp arrives as a pinned git submodule
git clone --recursive https://github.com/igna/linuxwhisperpipe
cd linuxwhisperpipe

# 2. build (CPU backend, no extra setup; takes a few minutes on first run)
make

# 3. download a model (fetched into whisper.cpp/models/, where the app
#    looks by default — no flag needed afterwards)
./download-model.sh          # ggml-small (default)
# ./download-model.sh base   # fastest, lower quality
# ./download-model.sh large-v3   # best quality, slower

# 4. find the monitor source of the device your audio goes through
#    (the one thing that differs per machine)
pactl list sources short | grep monitor

# 5. run
./whisperpipe -s <your.device>.monitor
```

`Ctrl+C` stops it. Save a transcript with:

```sh
./whisperpipe -s <your.device>.monitor | tee transcript.log
```

### GPU (optional)

CPU works out of the box. For faster inference rebuild with a GPU backend
(driver packages for your card required):

```sh
make distclean
make GPU=Vulkan   # AMD (e.g. RX 9070 XT) / Intel / NVIDIA
make GPU=CUDA     # NVIDIA
make GPU=HIP      # AMD (ROCm)
```

## Options

| Flag | Meaning | Default |
| --- | --- | --- |
| `-s, --source NAME` | Monitor source to listen on (see step 4) | machine-specific — always pass it |
| `-m, --model PATH` | Path to a GGML model file | `whisper.cpp/models/ggml-small.bin` (next to the binary) |
| `-t, --threads N` | Inference threads | `6` (match your physical core count) |
| `-r, --rms THRESHOLD` | RMS gate — chunks quieter than this are skipped | `100` |
| `-c, --chunk SECS` | Audio chunk length, 1–10 s (higher = more context, more latency) | `3` |
| `-h, --help` | Help | |

Tuning tips:

- **Missing quiet speech** → lower the gate (`-r 50`) or raise the chunk (`-c 5`).
- **Feels laggy** → smaller model (`base`) or a GPU build.
- Any [GGML model](https://huggingface.co/ggerganov/whisper.cpp) works via `-m`.

## Troubleshooting

- **`pa_simple_new failed`** — no Pulse/PipeWire running, or wrong source name.
  Check `pactl list sources`; on PipeWire make sure `pipewire-pulse` is active.
- **`could not load model`** — run `./download-model.sh` or pass `-m`.
- **No text although audio plays** — you're monitoring a source that receives
  no audio. Pick the monitor of the *output device* you actually use (step 4).
- **Slow on CPU** — use a smaller model (`./download-model.sh base`) or build
  with `make GPU=Vulkan`.

## How it works

The main thread records 16 kHz mono PCM in chunks from the monitor source.
A worker thread skips chunks below the RMS gate, runs the rest through
whisper.cpp with automatic language detection, and prints each segment with
its wall-clock time, millisecond offset within the chunk, and detected
language to stdout.

## License

MIT (see [LICENSE](LICENSE); whisper.cpp is MIT too).
