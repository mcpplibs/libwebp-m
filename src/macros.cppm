// mcpplibs.mwrap.libwebp.macros — the macros of libwebp's public headers (a
// named module cannot export a macro), under their original names in
// mcpplibs::mwrap::libwebp::macros:
//
//   - the ABI versions the headers were written for (WEBP_DECODER_ABI_VERSION,
//     WEBP_ENCODER_ABI_VERSION, WEBP_DEMUX_ABI_VERSION, WEBP_MUX_ABI_VERSION),
//     WEBP_MAX_DIMENSION and libsharpyuv's version (SHARPYUV_VERSION_MAJOR,
//     _MINOR, _PATCH, SHARPYUV_VERSION) as `inline constexpr int`;
//   - WEBP_ABI_IS_INCOMPATIBLE and SHARPYUV_MAKE_VERSION as constexpr
//     functions.
//
// The demux and mux ABI versions are constants of their headers: they need
// neither feature. The rest are declaration annotations (WEBP_EXTERN,
// WEBP_INLINE, WEBP_NODISCARD, SHARPYUV_EXTERN, SHARPYUV_INLINE) and include
// guards; tools/check_exports.py accounts for every macro.
module;
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/demux.h>
#include <webp/mux.h>
#include <sharpyuv/sharpyuv.h>

namespace mwrap_libwebp_capture {
inline constexpr int value_WEBP_DECODER_ABI_VERSION = WEBP_DECODER_ABI_VERSION;
inline constexpr int value_WEBP_ENCODER_ABI_VERSION = WEBP_ENCODER_ABI_VERSION;
inline constexpr int value_WEBP_DEMUX_ABI_VERSION = WEBP_DEMUX_ABI_VERSION;
inline constexpr int value_WEBP_MUX_ABI_VERSION = WEBP_MUX_ABI_VERSION;
inline constexpr int value_WEBP_MAX_DIMENSION = WEBP_MAX_DIMENSION;
inline constexpr int value_SHARPYUV_VERSION_MAJOR = SHARPYUV_VERSION_MAJOR;
inline constexpr int value_SHARPYUV_VERSION_MINOR = SHARPYUV_VERSION_MINOR;
inline constexpr int value_SHARPYUV_VERSION_PATCH = SHARPYUV_VERSION_PATCH;
inline constexpr int value_SHARPYUV_VERSION = SHARPYUV_VERSION;
constexpr bool abi_is_incompatible(int a, int b) { return WEBP_ABI_IS_INCOMPATIBLE(a, b); }
constexpr int make_version(int major, int minor, int patch) { return SHARPYUV_MAKE_VERSION(major, minor, patch); }
}  // namespace mwrap_libwebp_capture

#undef WEBP_DECODER_ABI_VERSION
#undef WEBP_ENCODER_ABI_VERSION
#undef WEBP_DEMUX_ABI_VERSION
#undef WEBP_MUX_ABI_VERSION
#undef WEBP_MAX_DIMENSION
#undef WEBP_ABI_IS_INCOMPATIBLE
#undef SHARPYUV_VERSION_MAJOR
#undef SHARPYUV_VERSION_MINOR
#undef SHARPYUV_VERSION_PATCH
#undef SHARPYUV_VERSION
#undef SHARPYUV_MAKE_VERSION

export module mcpplibs.mwrap.libwebp.macros;

export namespace mcpplibs::mwrap::libwebp::macros {

// The ABI versions (MAJOR(8b) + MINOR(8b)) passed to the *Internal functions.
inline constexpr int WEBP_DECODER_ABI_VERSION = mwrap_libwebp_capture::value_WEBP_DECODER_ABI_VERSION;
inline constexpr int WEBP_ENCODER_ABI_VERSION = mwrap_libwebp_capture::value_WEBP_ENCODER_ABI_VERSION;
inline constexpr int WEBP_DEMUX_ABI_VERSION = mwrap_libwebp_capture::value_WEBP_DEMUX_ABI_VERSION;
inline constexpr int WEBP_MUX_ABI_VERSION = mwrap_libwebp_capture::value_WEBP_MUX_ABI_VERSION;

// The largest width or height the encoder accepts.
inline constexpr int WEBP_MAX_DIMENSION = mwrap_libwebp_capture::value_WEBP_MAX_DIMENSION;

inline constexpr int SHARPYUV_VERSION_MAJOR = mwrap_libwebp_capture::value_SHARPYUV_VERSION_MAJOR;
inline constexpr int SHARPYUV_VERSION_MINOR = mwrap_libwebp_capture::value_SHARPYUV_VERSION_MINOR;
inline constexpr int SHARPYUV_VERSION_PATCH = mwrap_libwebp_capture::value_SHARPYUV_VERSION_PATCH;
inline constexpr int SHARPYUV_VERSION = mwrap_libwebp_capture::value_SHARPYUV_VERSION;

// Whether two ABI versions differ in their major byte.
constexpr bool WEBP_ABI_IS_INCOMPATIBLE(int a, int b) { return mwrap_libwebp_capture::abi_is_incompatible(a, b); }

// (MAJOR << 24) | (MINOR << 16) | PATCH, as SharpYuvGetVersion() answers.
constexpr int SHARPYUV_MAKE_VERSION(int major, int minor, int patch) {
    return mwrap_libwebp_capture::make_version(major, minor, patch);
}

}  // namespace mcpplibs::mwrap::libwebp::macros
