# linuxwhisperpipe

Real-time local speech-to-text for Linux. Captures your system audio through the PipeWire/PulseAudio API,
gates out silence, and transcribes it with
[whisper.cpp](https://github.com/ggml-org/whisper.cpp) — fully local,
automatic language detection, timestamped output.

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

## Step-by-step

**1. Clone** — whisper.cpp is a pinned git submodule; `--recursive` fetches it:

```sh
git clone --recursive https://github.com/Igna-Mendez/linuxwhisperpipe
cd linuxwhisperpipe
```

**2. Build** — builds whisper.cpp as static libs, then links the app.
The resulting `./whisperpipe` is self-contained (no `LD_LIBRARY_PATH`, no
shared-lib setup). First build takes a few minutes.

```sh
make            # CPU backend — works out of the box
```

**3. Download a model** — fetched into `whisper.cpp/models/`, where the app
looks by default, so no flag is needed afterwards:

```sh
./download-model.sh           # ggml-small (the app's built-in default recommended for CPU use)
# ./download-model.sh base    # fastest, lower quality
# ./download-model.sh large-v3  # best (quality/speed recommended for GPU use)
```

**4. Find your monitor source** — 

```sh
pactl list sources short | grep monitor
```

**5. Run**

```sh
./whisperpipe -s <your.device>.monitor
```

`Ctrl+C` stops it. To keep a transcript:

```sh
./whisperpipe -s <your.device>.monitor | tee transcript.log
```

## Options

| Flag | Meaning | Default |
| --- | --- | --- |
| `-s, --source NAME` | Monitor source to listen on (step 4) | a build-time placeholder — effectively always pass it |
| `-m, --model PATH` | GGML model file | `whisper.cpp/models/ggml-small.bin` next to the binary |
| `-t, --threads N` | Inference threads | `6` — match your physical core count |
| `-r, --rms THRESHOLD` | Silence gate: chunks quieter than this are skipped | `100` |
| `-c, --chunk SECS` | Audio chunk length, 1–10 s (higher = more context, more latency) | `3` |
| `-h, --help` | Help | |

Tuning:

- **Missing quiet speech** → lower the gate (`-r 50`) or raise the chunk (`-c 5`)
- **Feels laggy** → smaller model (`base`) or a GPU build
- Any [GGML model](https://huggingface.co/ggerganov/whisper.cpp) works via `-m`

### GPU (optional)

CPU works out of the box but quality is not the best, for best quality/speed rebuild with a GPU backend
(driver packages for your card required):

```sh
make distclean
make GPU=Vulkan   # AMD / Intel / NVIDIA
make GPU=CUDA     # NVIDIA
make GPU=HIP      # AMD (ROCm)
```

## Troubleshooting

- **`pa_simple_new failed`** — no Pulse/PipeWire running, or wrong source
  name. Check `pactl list sources`; on PipeWire make sure `pipewire-pulse`
  is active.
- **`could not load model`** — run `./download-model.sh` or pass `-m`.
- **No text although audio plays** — you're monitoring a source that
  receives no audio. Pick the monitor of the *output device* you actually
  use (step 4).
- **Slow on CPU** — use a smaller model or build with GPU for instance `make GPU=Vulkan`.

## How it works

The main thread records 16 kHz mono PCM in chunks from the monitor source.
A worker thread skips chunks below the RMS gate and runs the rest through
whisper.cpp with automatic language detection, printing each segment with
its wall-clock time, offset within the chunk, and detected language to
stdout.

## License

MIT (see [LICENSE](LICENSE); whisper.cpp is MIT too). YOU CAN COPY DISTRIBUTE AND DO WHATEVER YOU WANT WITH THIS CODE BUT MONETIZING IT
