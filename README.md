# Kamkast

## Info

This is a command-line program which provides HTTP video/audio live streaming server. 

It serves stream from various media sources found in Linux system. 

The following sources are supported right now:

Video:

- v4l2 devices (e.g. USB web cams)
- X11 screen capture
- DroidCam (camera in Sailfish OS)
- Lipstick screen capture (wayland compositor in Sailfish OS)

Audio:

- PulseAudio sources

## Usage

Kamkast is a command-line tool. Run `--help` to see all possible configuration options.

```
./kamkast --help
```

## Installation

There is no specific instalation procedure. Program is a single executable file.

## Building from sources

### General

```
git clone https://github.com/mkiol/kamkast.git

cd kamkast
mkdir build
cd build

cmake ../
make
```

### Sailfish OS

Example for `SailfishOS-4.4.0.58-aarch64` target:

```
git clone https://github.com/mkiol/kamkast.git

cd kamkast
mkdir build
cd build

sfdk config --session specfile=../../sfos/harbour-kamkast.spec
sfdk config --session target=SailfishOS-4.4.0.58-aarch64
sfdk cmake ../../ -Dwith_v4l2=0 -Dwith_droidcam=1 -Dwith_sfos=1 -Dwith_sfos_screen_capture=1
sfdk package
```

### Raspberry Pi OS

```
sudo apt install libpulse-dev libx11-dev

git clone https://github.com/mkiol/kamkast.git

cd kamkast
mkdir build
cd build

cmake ../ -Dwith_nvenc=0
make
```

## Download

- Binaries for `x86_64`, `arm`, `aarch64` as well as flatpak packages are in [Releases](https://github.com/mkiol/kamkast/releases). 
- Sailfish OS packages are available on [OpenRepos](https://openrepos.net/content/mkiol/kamkast)

## License

Kamkast is developed as an open source project under [Mozilla Public License Version 2.0](https://www.mozilla.org/MPL/2.0/).

