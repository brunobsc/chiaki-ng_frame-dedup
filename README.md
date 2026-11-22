
![chiaki-ng Logo](gui/res/chiaking-logo.svg)

# [chiaki-ng](https://chiaki-ng.github.io/chiaki-ng/)

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://chiaki-ng.github.io/chiaki-ng/).

> [!IMPORTANT]
> **This is an unofficial fork of chiaki-ng.** It is not made, endorsed or supported by the chiaki-ng project.
> For the official app, use the [official downloads](#official-downloads) below. Please report problems with this
> fork here, not to the chiaki-ng developers.

## About this fork: repeated-frame removal for frame generation

Many PS5 games render below 60 fps (often 30-45 and fluctuating) while the stream runs at 60 fps. The console
then sends the previous picture again to fill the gaps. Driver frame generation such as
**NVIDIA Smooth Motion** works on what the app presents, so it ends up interpolating between two identical
pictures and the motion does not get smoother.

This fork removes those repeated frames before they are shown, so frame generation only sees real, distinct
game frames:

- **Keep the stream at 60 fps** so every real frame the game renders reaches you, even when it fluctuates.
- **Repeated frames are detected automatically**, in two stages:
  1. a tiny encoded frame (the encoder's "nothing changed" frame) is a repeat outright;
  2. otherwise the decoded picture is compared with the previous one in 16x16 blocks; if no block changed
     beyond a small tolerance it is a repeat. This also catches repeats the encoder "refines" with spare bitrate.
- Repeats are still decoded (later frames depend on them), they are just not presented.
- Safety rules: key frames are never hidden, nothing is hidden right after packet loss, and at most 3 frames in a
  row can be hidden.
- **Both repeated-frame removal and NVIDIA Smooth Motion are on by default** in this fork (Linux).

Tested with synthetic streams (a 20-40 fps game in a 60 fps stream, HEVC 5/15 Mbps and H.264 10 Mbps): every
repeat removed, no real frame hidden. The comparison costs about 0.5 ms per 1080p frame. Real-world testing
across games is ongoing; feedback is welcome.

### Requirements

- Linux. The removal itself works on any GPU, but it only pays off together with frame generation.
- For NVIDIA Smooth Motion: GeForce **RTX 40 or 50** series and NVIDIA driver **580.82.07 or newer**.
- In chiaki-ng **Settings → Video**:
  - stream at **60 fps** (HEVC recommended)
  - hardware decoder: **vulkan**
  - **Frame Mixer: None** (the default). With a frame mixer on, presentation follows the display refresh and
    the removal has no effect.
- A display at **120 Hz or more** to see the generated frames.

### Download

Get the AppImage from this fork's [Releases](../../releases). It is built with the project's own AppImage
workflow, so it runs on most x86-64 Linux distributions.

```bash
chmod +x chiaki-ng.AppImage
./chiaki-ng.AppImage
```

### Options

All options are environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `CHIAKI_DEDUP` | `1` | `0` turns repeated-frame removal off |
| `NVPRESENT_ENABLE_SMOOTH_MOTION` | `1` | `0` turns NVIDIA Smooth Motion off |
| `CHIAKI_DEDUP_PIXEL` | `4.0` | Picture tolerance (0-255 per block). Lower hides fewer frames. `0` uses only the size check |
| `CHIAKI_DEDUP_RATIO` | `0.10` | A frame smaller than this fraction of the median frame size is a repeat |
| `CHIAKI_DEDUP_MIN_BYTES` | `256` | Frames smaller than this are always repeats |
| `CHIAKI_DEDUP_MAX_DROP` | `3` | Maximum frames hidden in a row |
| `CHIAKI_DEDUP_DEBUG` | `0` | `1` prints every decision to stderr, for tuning |

Examples:

```bash
CHIAKI_DEDUP=0 ./chiaki-ng.AppImage                         # Smooth Motion only, for comparison
NVPRESENT_ENABLE_SMOOTH_MOTION=0 CHIAKI_DEDUP=0 ./chiaki-ng.AppImage   # plain stream
CHIAKI_DEDUP_DEBUG=1 ./chiaki-ng.AppImage 2> dedup.log      # log decisions
```

Every 600 frames the regular chiaki-ng log also gets a summary line
(`Repeated-frame removal: N in, N hidden, N forced, median N bytes`).

### Building from source

The change lives in `lib/src/framededup.c` (detection), `lib/src/ffmpegdecoder.c` (wiring) and
`gui/src/qmlbackend.cpp` (skipping presentation). Build as usual for chiaki-ng; on distributions with a recent
OpenSSL (for example Arch / CachyOS) configure with `-DCHIAKI_USE_SYSTEM_CURL=ON`, because the bundled curl does
not build against it. Unit tests: `build/test/chiaki-unit`.

### Resumo em português

Fork **não oficial** do chiaki-ng. Remove automaticamente os quadros repetidos que o PS5 envia quando o jogo
roda abaixo de 60 fps, para que a geração de quadros (NVIDIA Smooth Motion) trabalhe só com quadros reais.
Remoção e Smooth Motion vêm ligados por padrão. Use stream a 60 fps, decodificador **vulkan** e
**Frame Mixer: None**. O Smooth Motion precisa de RTX 40/50 e driver NVIDIA 580.82.07 ou mais novo. Baixe o
AppImage em [Releases](../../releases). Para desligar: `CHIAKI_DEDUP=0` e/ou `NVPRESENT_ENABLE_SMOOTH_MOTION=0`.

---

## Discord
[chiaki-ng community Discord](https://discord.gg/tAMbRuwXDH)

## Official downloads
Only download chiaki-ng from these sources:

- [GitHub Releases](https://github.com/chiaki-ng/chiaki-ng/releases)
- [Flathub](https://flathub.org/apps/io.github.streetpea.Chiaki4deck)
- winget: `winget install --id=StreetPea.chiaki-ng -e`
- Homebrew: `brew install --cask streetpea/streetpea/chiaki-ng`

Other websites offering chiaki-ng downloads, such as chiaki-ng.com, are not run by this project, and we can't vouch for the files they serve.

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.
