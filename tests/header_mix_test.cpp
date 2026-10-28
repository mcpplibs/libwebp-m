// The raw headers and the import in one translation unit (the headers first):
// the module's names are the very same entities, the header's macros equal
// the mapped constants, and the header's static inline functions and the
// module's adaptations give the same results. While a header's macro is
// defined it expands even inside a qualified name, so
// `macros::WEBP_MAX_DIMENSION` is only reachable with the macro set aside;
// and both static inline forms are visible, so name them qualified.
#include <webp/decode.h>
#include <webp/encode.h>
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.libwebp;

namespace {
constexpr int raw_max = WEBP_MAX_DIMENSION, raw_decoder_abi = WEBP_DECODER_ABI_VERSION;
}

#pragma push_macro("WEBP_MAX_DIMENSION")
#pragma push_macro("WEBP_DECODER_ABI_VERSION")
#undef WEBP_MAX_DIMENSION
#undef WEBP_DECODER_ABI_VERSION
static_assert(mcpplibs::mwrap::libwebp::macros::WEBP_MAX_DIMENSION == raw_max);
static_assert(mcpplibs::mwrap::libwebp::macros::WEBP_DECODER_ABI_VERSION == raw_decoder_abi);
#pragma pop_macro("WEBP_DECODER_ABI_VERSION")
#pragma pop_macro("WEBP_MAX_DIMENSION")

TEST(HeaderMix, SameEntitiesAndValues) {
    static_assert(std::is_same_v<decltype(&::WebPDecodeRGBA), std::uint8_t* (*)(const std::uint8_t*, std::size_t, int*, int*)>);
    static_assert(std::is_same_v<WebPConfig, ::WebPConfig> && std::is_same_v<WebPPicture, ::WebPPicture>);
    static_assert(MODE_RGBA == ::MODE_RGBA && VP8_ENC_ERROR_BAD_DIMENSION == ::VP8_ENC_ERROR_BAD_DIMENSION);

    // The header's WebPConfigInit and the module's: the same configuration
    // (the reserved padding is left as it was, by both).
    ::WebPConfig raw, adapted;
    std::memset(&raw, 0x5a, sizeof raw);
    std::memset(&adapted, 0x5a, sizeof adapted);
    ASSERT_TRUE(::WebPConfigInit(&raw));
    ASSERT_TRUE(mcpplibs::mwrap::libwebp::WebPConfigInit(&adapted));
    EXPECT_EQ(std::memcmp(&raw, &adapted, sizeof raw), 0);
    ASSERT_TRUE(::WebPConfigPreset(&raw, WEBP_PRESET_ICON, 33.f));
    ASSERT_TRUE(mcpplibs::mwrap::libwebp::WebPConfigPreset(&adapted, WEBP_PRESET_ICON, 33.f));
    EXPECT_EQ(std::memcmp(&raw, &adapted, sizeof raw), 0);
    for (int mode = MODE_RGB; mode < MODE_LAST; ++mode) {
        const auto m = static_cast<WEBP_CSP_MODE>(mode);
        EXPECT_EQ(::WebPIsAlphaMode(m), mcpplibs::mwrap::libwebp::WebPIsAlphaMode(m)) << mode;
        EXPECT_EQ(::WebPIsPremultipliedMode(m), mcpplibs::mwrap::libwebp::WebPIsPremultipliedMode(m)) << mode;
        EXPECT_EQ(::WebPIsRGBMode(m), mcpplibs::mwrap::libwebp::WebPIsRGBMode(m)) << mode;
    }

    // Encoded through the header's spelling, decoded through the module's.
    const std::array<std::uint8_t, 16> pixels{255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 9, 9, 9, 9};
    std::uint8_t* file = nullptr;
    const std::size_t size = ::WebPEncodeLosslessRGBA(pixels.data(), 2, 2, 8, &file);
    ASSERT_GT(size, 0u);
    WebPBitstreamFeatures a, b;
    ASSERT_EQ(::WebPGetFeatures(file, size, &a), VP8_STATUS_OK);
    ASSERT_EQ(mcpplibs::mwrap::libwebp::WebPGetFeatures(file, size, &b), VP8_STATUS_OK);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof a), 0);
    WebPFree(file);
}
