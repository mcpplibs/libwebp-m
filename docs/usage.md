# Using mcpplibs.mwrap.libwebp

`import mcpplibs.mwrap.libwebp;` declares libwebp 1.5.0's API as its headers do. Types, enumerators and functions are the upstream globals, the very same entities as the headers', so a translation unit may include a header too. The codec is `compat.libwebp` 1.5.0, built from the same release.

## Modules

| Module | What it holds |
|---|---|
| `mcpplibs.mwrap.libwebp` | `webp/types.h`, `webp/decode.h`, `webp/encode.h`, `webp/mux_types.h`: 23 types (`WebPConfig`, `WebPPicture`, `WebPDecoderConfig`, `WebPBitstreamFeatures`, `WebPMemoryWriter`, `WebPData` …), 59 enumerators (`MODE_RGBA`, `VP8_STATUS_OK`, `WEBP_PRESET_PHOTO`, `VP8_ENC_ERROR_BAD_DIMENSION`, `ALPHA_FLAG` …) and 72 functions (`WebPGetInfo`, `WebPDecodeRGBA`, `WebPEncodeRGBA`, `WebPEncodeLosslessRGBA`, `WebPEncode`, `WebPPictureImportRGBA`, `WebPINewDecoder`, `WebPFree` …); 13 static inline functions adapted; re-exports the two below |
| `mcpplibs.mwrap.libwebp.sharpyuv` | `sharpyuv/sharpyuv.h`, `sharpyuv/sharpyuv_csp.h`: 6 types, 25 enumerators, 6 functions; `SharpYuvOptionsInit` adapted |
| `mcpplibs.mwrap.libwebp.macros` | the macros, in `mcpplibs::mwrap::libwebp::macros` |
| `mcpplibs.mwrap.libwebp.demux` | `webp/demux.h`, with the `demux` feature: 8 types, 10 enumerators, 20 functions (`WebPDemuxGetFrame`, `WebPAnimDecoderGetNext` …); `WebPDemux`, `WebPDemuxPartial`, `WebPAnimDecoderOptionsInit`, `WebPAnimDecoderNew` adapted; re-exports the root |
| `mcpplibs.mwrap.libwebp.mux` | `webp/mux.h`, with the `mux` feature: 7 types, 17 enumerators, 27 functions (`WebPMuxSetChunk`, `WebPAnimEncoderAdd` …); `WebPMuxNew`, `WebPMuxCreate`, `WebPAnimEncoderOptionsInit`, `WebPAnimEncoderNew` adapted; re-exports the root |

The root cannot re-export `demux` and `mux` (mcpp's scanner admits no import under a feature test): import them by name. `webp/format_constants.h` is not public upstream (it is not installed) and is not exported.

## The static inline API

Twenty-two functions of the headers are `static inline`: the initialisers that pass the header's ABI version to an `*Internal` function (`WebPConfigInit`, `WebPConfigPreset`, `WebPPictureInit`, `WebPInitDecoderConfig`, `WebPInitDecBuffer`, `WebPGetFeatures`, `WebPAnimEncoderOptionsInit`, `WebPAnimEncoderNew`, `WebPAnimDecoderOptionsInit`, `WebPAnimDecoderNew`, `WebPDemux`, `WebPDemuxPartial`, `WebPMuxNew`, `WebPMuxCreate`, `SharpYuvOptionsInit`), the colourspace tests (`WebPIsAlphaMode`, `WebPIsPremultipliedMode`, `WebPIsRGBMode`), `WebPIDecGetYUV` and the `WebPData` helpers (`WebPDataInit`, `WebPDataClear`, `WebPDataCopy`).

Internal linkage cannot be exported. Each is adapted as a function with the same name and signature (and `[[nodiscard]]` where the header has `WEBP_NODISCARD`) in `mcpplibs::mwrap::libwebp`, defined out of line by calling upstream's own inline body, so it passes the same ABI version and gives the same result. `using namespace mcpplibs::mwrap::libwebp;` makes code read as it does in C. A translation unit that also includes the header sees both: call the header's as `::WebPConfigInit`. The `*Internal` functions are exported too.

## Decoding and encoding, in brief

```cpp
import std;
import mcpplibs.mwrap.libwebp;
using namespace mcpplibs::mwrap::libwebp;  // WebPConfigInit, WebPPictureInit, WebPGetFeatures …

// RGBA (width * height * 4 bytes) to WebP: lossy at `quality` (0 … 100), or lossless.
std::vector<std::uint8_t> encode(std::span<const std::uint8_t> rgba, int width, int height, bool lossless, float quality) {
    WebPConfig config;
    WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPPictureInit(&picture)) return {};
    config.lossless = lossless;
    config.quality = quality;   // lossy: visual quality; lossless: effort
    config.method = 4;          // 0 (fast) … 6 (smallest)
    config.exact = lossless;    // keep the colour under alpha 0 too
    picture.use_argb = 1;
    picture.width = width;
    picture.height = height;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    std::vector<std::uint8_t> out;
    if (WebPPictureImportRGBA(&picture, rgba.data(), width * 4) && WebPEncode(&config, &picture))
        out.assign(writer.mem, writer.mem + writer.size);   // else picture.error_code says why
    WebPPictureFree(&picture);
    WebPMemoryWriterClear(&writer);
    return out;
}

// WebP to RGBA.
std::vector<std::uint8_t> decode(std::span<const std::uint8_t> webp, int& width, int& height) {
    std::uint8_t* pixels = WebPDecodeRGBA(webp.data(), webp.size(), &width, &height);
    if (!pixels) return {};
    std::vector<std::uint8_t> out(pixels, pixels + std::size_t(width) * height * 4);
    WebPFree(pixels);
    return out;
}
```

- **Inspecting.** `WebPGetInfo` gives the size from the first bytes; `WebPGetFeatures` adds `has_alpha`, `has_animation` and `format` (1 lossy, 2 lossless, 0 mixed or animated). An animated file decodes with `WebPAnimDecoder`, not `WebPDecode`.
- **The simple API.** `WebPDecodeRGBA` / `BGRA` / `ARGB` / `RGB` / `BGR` / `YUV` allocate the result (free it with `WebPFree`); the `…Into` forms write into the caller's buffer with its stride. `WebPEncodeRGBA` (lossy at a quality) and `WebPEncodeLosslessRGBA` allocate the file (free it with `WebPFree`) and answer its size, 0 on failure. They use the default settings: `exact` is off, so the colour under fully transparent pixels may change (alpha itself is kept).
- **The advanced API.** `WebPConfig` holds every setting (`quality`, `method`, `lossless`, `exact`, `alpha_quality`, `near_lossless`, `use_sharp_yuv`, `target_size` …); `WebPConfigPreset` and `WebPConfigLosslessPreset(config, 0 … 9)` start from upstream's presets, `WebPValidateConfig` checks one. `WebPPicture` takes RGBA, BGRA, RGB … (`WebPPictureImport*`), can be cropped, rescaled, copied and compared (`WebPPictureDistortion`); `stats` and `progress_hook` report on an encoding, and a hook answering 0 aborts it (`VP8_ENC_ERROR_USER_ABORT`). `WebPDecoderConfig` decodes with scaling, cropping, flipping, without fancy upsampling, into any `WEBP_CSP_MODE` (premultiplied `MODE_rgbA` …) or into the caller's memory.
- **Incremental decoding.** `WebPINewDecoder` / `WebPINewRGB` / `WebPINewYUVA`, then `WebPIAppend` (or `WebPIUpdate`) as data arrives: `VP8_STATUS_SUSPENDED` until the image is complete; `WebPIDecGetRGB` / `WebPIDecGetYUVA` give the rows decoded so far.
- **Animation** (both features). `WebPAnimEncoderNew(width, height, options)`, then `WebPAnimEncoderAdd(encoder, picture, timestamp_ms, config)` per frame and once with a null picture at the end time, `WebPAnimEncoderAssemble` into a `WebPData` (free with `WebPDataClear`). `WebPAnimDecoderNew(data, options)` with `color_mode` `MODE_RGBA`, `MODE_BGRA`, `MODE_rgbA` or `MODE_bgrA`; `WebPAnimDecoderGetNext` gives the whole canvas and the frame's end time, owned by the decoder until the next call. `WebPAnimDecoderGetInfo` has the canvas, frame and loop counts.
- **The container.** `WebPDemux` reads frames (`WebPDemuxGetFrame`), chunks (`WebPDemuxGetChunk(demux, "ICCP", 1, &it)`, `"EXIF"`, `"XMP "`) and features (`WebPDemuxGetI`); `WebPMux` adds or removes chunks and frames and `WebPMuxAssemble` writes the file.
- **Errors** never abort: functions answer 0, `nullptr`, a `VP8StatusCode`, a `WebPEncodingError` in `picture.error_code`, a `WebPMuxError`, or a message (`WebPAnimEncoderGetError`).

## Macros

A named module cannot export a macro. `mcpplibs::mwrap::libwebp::macros` holds them under their original names:

- **Constants** as `inline constexpr int`: `WEBP_DECODER_ABI_VERSION` (0x0209), `WEBP_ENCODER_ABI_VERSION` (0x0210), `WEBP_DEMUX_ABI_VERSION` (0x0107), `WEBP_MUX_ABI_VERSION` (0x0109), `WEBP_MAX_DIMENSION` (16383), `SHARPYUV_VERSION_MAJOR` / `_MINOR` / `_PATCH`, `SHARPYUV_VERSION`.
- **Functions**: `WEBP_ABI_IS_INCOMPATIBLE(a, b)` and `SHARPYUV_MAKE_VERSION(major, minor, patch)`, constexpr.

No macro has to stay a macro, so there is no side header. [tools/check_exports.py](../tools/check_exports.py) accounts for all 24 macros of the public headers: 11 mapped, and excluded with their reasons the declaration annotations (`WEBP_EXTERN`, `WEBP_INLINE`, `WEBP_NODISCARD`, `SHARPYUV_EXTERN`, `SHARPYUV_INLINE`) and 8 include guards. While a header's macro is defined it expands even inside a qualified name, so with `<webp/encode.h>` included, `macros::WEBP_MAX_DIMENSION` is reachable only with the macro set aside (see [tests/header_mix_test.cpp](../tests/header_mix_test.cpp)).

## The codec as built

compat.libwebp compiles every source of `src/dec`, `src/enc`, `src/dsp`, `src/utils` and `sharpyuv` into one archive (`src/demux` and `src/mux` join it with the features), with a generated `config.h`:

- **SIMD**: SSE2 on x86-64 and NEON on arm64, the baselines of their architectures, chosen at run time by libwebp's own CPU dispatch; SSE4.1 is off (its sources would need a per-file target flag), as are MIPS/MSA. wasm32-emscripten builds the portable C.
- **Threads**: `WEBP_USE_THREAD` is not defined. `WebPConfig::thread_level` and `WebPDecoderOptions::use_threads` are accepted and the work runs on the calling thread, with the same result. Without it, libwebp's one-time DSP initialisation is not guarded by a mutex: a program that starts encoding or decoding on several threads at once should first encode or decode once (any image) on one thread.
- **Determinism**: the same picture and settings give the same bytes everywhere: checked, not assumed. libwebp's SSE2 and NEON paths give the results of its C paths, and the few `pow` calls of the encoder (quality to quantizer, gamma tables) are rounded to integers. [tests/determinism_test.cpp](../tests/determinism_test.cpp) pins the FNV-1a hashes of lossy files (quality 50/75/90, methods 0/4/6), lossless files (levels 0/6/9) and decoded pixels (with and without fancy upsampling), and [tests/anim](../tests/anim) those of lossless and lossy animations; CI checks them on Linux x86_64 (GCC, LLVM) and arm64, macOS arm64, Windows x86_64 and wasm32-emscripten, in the dev and release profiles. [tools/run_conformance.py](../tools/run_conformance.py) checks that the bytes are those of upstream's `cwebp` and `dwebp` built by upstream's own makefile.
- **Not built**: upstream's tools and their image readers (`examples/`, `imageio/`, `extras/`), GIF conversion, the SWIG and JavaScript bindings.

## Checks

- `mcpp test`: the simple and advanced decoding and encoding API through the module (lossy at several qualities and lossless, with alpha; presets, validation, statistics, progress and abort, distortion, crop and rescale; scaled, flipped, premultiplied, YUVA and caller-memory decoding; incremental decoding; errors on garbage, truncation and oversized pictures), the adapted static inline API, `WebPData`, sharpyuv, the constants, the headers mixed with the import, and the pinned hashes. `tests/anim` (its own project, with both features): animations encoded and decoded frame by frame, every colour mode, the demuxer, chunks and frames through the muxer, the error paths, pinned hashes. CI runs both on Linux, macOS, Windows and under node, in the dev and release profiles.
- `python3 tools/check_exports.py`: the modules are what [tools/gen_exports.py](../tools/gen_exports.py) writes from the pinned headers; every declaration is exported by exactly one module, every static inline function adapted, every macro mapped or excluded with its reason.
- `python3 tools/run_conformance.py`: [examples/webpcodec](../examples/webpcodec) against upstream's `cwebp` and `dwebp` 1.5.0, built from the release by its `makefile.unix`: 76 comparisons (three inputs, eight lossy and lossless settings, each file decoded with and without fancy upsampling, and upstream's `examples/test.webp`), all byte-identical. Upstream's `examples/test_ref.ppm` predates libwebp's current YUV to RGB rounding: `dwebp` 1.5.0 itself decodes `test.webp` within 1 of it, and so does the module.
