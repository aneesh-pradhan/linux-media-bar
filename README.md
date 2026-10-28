# Linux Media Bar

This is meant to be a straightforward media server written in C++ to play back
videos from a computer that acts as the server/host to a PlayStation 3 on the same
network. 

## How it works

- Announces itself on the network with SSDP, so the PS3 lists it under Video.
- Serves a UPnP ContentDirectory for browsing the media folder.
- Streams `.mp4` files as they are, with HTTP range requests so seeking works.
- Transcodes `.mkv`, `.avi`, `.m2ts` and `.ts` on the fly with `ffmpeg` to
  H.264 and AAC in MPEG-TS, which the PS3 can play.

## Building

Needs CMake 3.20+ and a C++20 compiler. `ffmpeg` must be on your `PATH` for
transcoding.

```bash
cmake -B build
cmake --build build
```

Add `-DLMB_ENABLE_SANITIZERS=ON` for an ASan and UBSan build.

## Running

```bash
./build/linux-media-bar --media-root ~/Videos
```

Options: `--media-root PATH`, `--http-port PORT` (default 5001) and
`--ssdp-port PORT` (default 1900).

The network interface, bind address, server name and UUID are set in
`SsdpConfig` (`src/network/ssdp/ssdp_server.h`). Change them to match your
machine before building.

License is MIT (see LICENSE file). I have written this program for myself so there might be personalized
names and configs in the code, but anyone is free to use the code as is. 
