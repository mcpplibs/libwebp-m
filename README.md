# libwebp

> libwebp, the WebP image codec, through C++23 modules.

**English** · [简体中文](README.zh.md) · [Upstream](https://github.com/webmproject/libwebp) · [Releases](https://github.com/mcpplibs/libwebp-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![libwebp](https://img.shields.io/badge/libwebp-BSD--3--Clause-blue.svg)](upstream/libwebp/COPYING)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/libwebp-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/libwebp-m/actions/workflows/ci.yml)

libwebp 1.5.0 decodes and encodes WebP: lossy (VP8) and lossless (VP8L),
with alpha, still and animated. The upstream C API is unchanged:
`WebPGetInfo`, `WebPDecodeRGBA`, `WebPEncodeRGBA`, `WebPEncodeLosslessRGBA`,
`WebPEncode` with `WebPConfig` and `WebPPicture`, `WebPAnimEncoder` and
`WebPAnimDecoder` are the upstream globals of the headers. The codec is
`compat.libwebp` 1.5.0. The headers' `static inline` functions
(`WebPConfigInit`, `WebPPictureInit`, `WebPGetFeatures` …) are adapted under
the same names, and the macros are constants. On every CI run the bytes
written and the pixels read are those of upstream's own `cwebp` and `dwebp`,
and pinned hashes hold on every target, the Web included.

## Modules

- `import mcpplibs.mwrap.libwebp;`: decoding (`webp/decode.h`), encoding (`webp/encode.h`), `WebPData` and the animation enums (`webp/mux_types.h`), re-exporting the two below
- `import mcpplibs.mwrap.libwebp.sharpyuv;`: libsharpyuv, the sharp RGB to YUV conversion (`SharpYuvConvert` …)
- `import mcpplibs.mwrap.libwebp.macros;`: `WEBP_MAX_DIMENSION`, the ABI versions, `WEBP_ABI_IS_INCOMPATIBLE` … in `mcpplibs::mwrap::libwebp::macros`
- `import mcpplibs.mwrap.libwebp.demux;`: `WebPDemux`, `WebPAnimDecoder`, with the `demux` feature
- `import mcpplibs.mwrap.libwebp.mux;`: `WebPMux`, `WebPAnimEncoder`, with the `mux` feature

> More Modules API -> [Usage and API](docs/usage.md)

## Getting started

```sh
mcpp new pictures --template mcpplibs.mwrap.libwebp@1.5.0
cd pictures
mcpp run
```

The default template encodes a picture with alpha lossy at three qualities
and lossless, decodes each back to RGBA and prints its size and PSNR. The
`animation` template (`--template mcpplibs.mwrap.libwebp@1.5.0:animation`)
encodes frames with `WebPAnimEncoder` and plays them back with
`WebPAnimDecoder`.

```toml
[dependencies]
"mcpplibs.mwrap.libwebp" = "1.5.0"
```

```cpp
import std;
import mcpplibs.mwrap.libwebp;

int main() {
    std::vector<std::uint8_t> rgba(64 * 64 * 4, 200);   // a light grey, opaque square
    std::uint8_t* webp = nullptr;
    const std::size_t size = WebPEncodeRGBA(rgba.data(), 64, 64, 64 * 4, 80.f, &webp);  // lossy, quality 80
    int width = 0, height = 0;
    std::uint8_t* pixels = WebPDecodeRGBA(webp, size, &width, &height);
    std::println("{} bytes, {} x {}", size, width, height);
    WebPFree(pixels);
    WebPFree(webp);
}
```

| Feature | Effect |
|---|---|
| `demux` | libwebpdemux (compat.libwebp's `src/demux`) and the `demux` module: `WebPDemux*`, `WebPAnimDecoder*` |
| `mux` | libwebpmux (compat.libwebp's `src/mux`) and the `mux` module: `WebPMux*`, `WebPAnimEncoder*` |

Neither is on by default. Animated WebP needs `demux` to read and `mux` to
write.

## The codec

`compat.libwebp` builds the release's `src/dec`, `src/enc`, `src/dsp`,
`src/utils` and `sharpyuv` (and `src/demux`, `src/mux` with the features)
with SSE2 on x86-64 and NEON on arm64, SSE4.1 off, and without
`WEBP_USE_THREAD`: `thread_level` and `use_threads` are accepted and run on
the calling thread. Its output is deterministic: the same picture and
settings give the same bytes on Linux, macOS, Windows and wasm32-emscripten,
with GCC or LLVM, dev or release, and the same bytes as upstream's `cwebp`.
Details and limits: [docs/usage.md](docs/usage.md).

## Examples

- [examples/webpcodec](examples/webpcodec): decodes WebP to PPM/PAM, encodes PPM/PAM to WebP (lossy or lossless) and
  prints a file's features, written against the module; [tools/run_conformance.py](tools/run_conformance.py) compares
  it with upstream's `cwebp` and `dwebp`.

## Build and testing

Linux (x86_64 GCC and LLVM, arm64 GCC), macOS, Windows and wasm32-emscripten
(under node) CI build the modules and run the tests in the dev and release
profiles, with and without the features; the native rows also run the export
audit ([tools/check_exports.py](tools/check_exports.py): the generated modules
are fresh, every declaration exported, every macro mapped or excluded with its
reason) and, but on Windows, the conformance run against upstream's tools. A
separate job scaffolds both templates as an installed package and runs them.

## License

[Apache-2.0](LICENSE) for the wrapper, by mcpp-community; libwebp remains
BSD-3-Clause with Google's patent grant
([upstream/libwebp/COPYING](upstream/libwebp/COPYING),
[upstream/libwebp/PATENTS](upstream/libwebp/PATENTS)).
