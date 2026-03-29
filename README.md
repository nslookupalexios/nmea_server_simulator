# nmea_server_simulator

Minimal deterministic TCP server that generates valid `GPGGA` NMEA-0183 sentences
for GNSS testing, localization pipelines, and controlled simulation workflows.

## Features

- Valid `GPGGA` sentence generation with NMEA checksum and `\r\n` termination
- UTC timestamp generation
- Single-client TCP streaming server
- Configurable static GNSS parameters
- Fixed-period transmission loop using POSIX `usleep()`
- Random injection of spurious non-ASCII bytes between consecutive sentences
- No dynamic memory allocation

## Project layout

```text
.
├── include/
│   └── nmea_server.h
├── src/
│   ├── main.c
│   └── nmea_server.c
├── CMakeLists.txt
└── README.md
```

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Run

```bash
./build/nmea_server
```

Then connect with:

```bash
nc 127.0.0.1 5000
```

## Default parameters

The initial implementation uses static values defined in `src/main.c`:

- Latitude: `45.4642`
- Longitude: `9.1900`
- Altitude: `120.0 m`
- Fix quality: `1`
- Satellites used: `10`
- HDOP: `0.9`
- Geoid separation: `47.0 m`
- Transmission period: `1000 ms`
- TCP port: `5000`
- Spurious bytes minimum length: `1`
- Spurious bytes maximum length: `6`

## Notes

- The server accepts one client at a time.
- When the client disconnects, the server returns to `accept()` and waits for a new
  connection.
- The current implementation injects a pseudo-random sequence of non-ASCII bytes
  in the range `0x80..0xFF` between one NMEA sentence and the next.
- The spurious sequence length is configurable and currently defaults to a random
  value between `1` and `6` bytes.
- The current version generates static coordinates only and is intended as a clean
  baseline for future replay, trajectory, and fault-injection extensions.
