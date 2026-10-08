# Pico Console V2 MIDI

![Front view](doc/front.jpg)

[![Build test](https://github.com/Crem2y/pico-console-v2-midi/actions/workflows/build_test.yml/badge.svg)](https://github.com/Crem2y/pico-console-v2-midi/actions/workflows/build_test.yml)

A MIDI player for the [Pico Console V2](https://github.com/Crem2y/pico-console-v2) handheld platform.

This project plays MIDI files using the platform's custom APU without SoundFonts.
MIDI events are translated into APU commands to approximate instrument sounds through synthesized audio.

## Features

- MIDI file loading from microSD
- PSRAM-backed MIDI file storage
- MIDI playback without SoundFonts
- Instrument sound approximation using the custom APU
- Playback, pause, and resume controls
- Volume control
- Built-in MIDI file browser

## Implementation

- **Platform**: Pico Console V2
- **File loading**: MIDI files loaded from microSD into external PSRAM
- **MIDI processing**: MIDI events translated into APU commands
- **Sound generation**: Instrument sounds approximated using the custom APU without SoundFonts
- **Audio output**: Pico Console V2 audio system

## Controls

| Action                | Joypad              |
|-----------------------|---------------------|
| Resume or select file | A                   |
| Pause                 | B                   |
| Volume Up             | L or START          |
| Volume Down           | R or SELECT         |
| Exit                  | SUB2                |

## How to build & upload firmware

1. Install CMake (at least version 3.13), Python 3, and a GCC cross compiler
```bash
sudo apt install cmake python3 build-essential gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```
2. Clone this repository with submodules:
```bash
git clone --recurse-submodules https://github.com/Crem2y/pico-console-v2-midi.git
```
3. Launch the build script:
```bash
./pico_build.sh
```
4. Press the reset button twice to enter bootloader mode.

5. Upload the generated `.uf2` file to your board.
    - if you already installed [picotool](https://github.com/raspberrypi/picotool), use this.
```bash
./pico_upload.sh
```

6. (Optional) Launch the clean script to remove build artifacts:
```bash
./pico_clean.sh
```

## Hardware

This application runs on the Pico Console V2 platform.

- [Pico Console V2 Firmware](https://github.com/Crem2y/pico-console-v2)
- [RP2350A Main Board](https://github.com/Crem2y/rp2350a_main_board)
- [Pico Console V2 PCB](https://github.com/Crem2y/pico-console-v2-pcb)

## License
- This project is licensed under the MIT License.  
- See [LICENSE](./LICENSE) for details.

### Third-party components
- Thank you to the many open-source contributors.
- See the `third_party_licenses/` directory for full details.