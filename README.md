# Kamkast

## Info

This is a command-line program which provides HTTP video/audio live streaming server. 

It serves stream from various media sources found in Linux system. 

The following sources are supported right now:

Video:

- v4l2 devices (e.g. USB web cams)
- X11 screen capture
- DroidCam (camera in Sailfish OS)
- Lipstick screen capture (Wayland display server in Sailfish OS)

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

cmake ../ -DCMAKE_BUILD_TYPE=Release
make
```

### Sailfish OS

Example for `SailfishOS-4.4.0.58-aarch64` target:

```
git clone https://github.com/mkiol/kamkast.git

cd kamkast
mkdir build
cd build

sfdk config --session specfile=../sfos/harbour-kamkast.spec
sfdk config --session target=SailfishOS-4.4.0.58-aarch64
sfdk cmake ../ -DCMAKE_BUILD_TYPE=Release -Dwith_sfos=ON -Dwith_sfos_screen_capture=ON
sfdk package
```

### Raspberry Pi OS

```
sudo apt install libpulse-dev libx11-dev

git clone https://github.com/mkiol/kamkast.git

cd kamkast
mkdir build
cd build

cmake ../ -DCMAKE_BUILD_TYPE=Release -Dwith_nvenc=0
make
```

## Download

- Binaries for `x86_64`, `arm`, `aarch64` as well as flatpak packages are in [Releases](https://github.com/mkiol/kamkast/releases). 
- Sailfish OS packages are available on [OpenRepos](https://openrepos.net/content/mkiol/kamkast)

## Plans for future development

- Support for [libcamera](https://libcamera.org/)
- Support for screen capture on more Wayland display servers (Kwin, Mutter) 

## License

Kamkast is developed as an open source project under [Mozilla Public License Version 2.0](https://www.mozilla.org/MPL/2.0/).

