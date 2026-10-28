// The same bytes on every target. One picture (a fixed pattern with alpha:
// gradients, edges, noise from a fixed LCG) encoded lossy at several
// qualities and methods, and lossless at several levels; then a lossy file
// decoded to RGBA. The FNV-1a hashes are pinned: CI checks them on Linux
// x86_64 (GCC, LLVM), Linux arm64, macOS arm64, Windows x86_64 and
// wasm32-emscripten, in the dev and release profiles. A failure prints the
// hash this target produced.
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.libwebp;

using namespace mcpplibs::mwrap::libwebp;

namespace {

constexpr int W = 128, H = 96;

std::vector<std::uint8_t> picture() {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4);
    std::uint32_t state = 12345;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            state = state * 1664525u + 1013904223u;
            const int noise = static_cast<int>(state >> 28);  // 0 … 15
            auto* p = &rgba[(static_cast<std::size_t>(y) * W + x) * 4];
            const bool disc = (x - 80) * (x - 80) + (y - 40) * (y - 40) < 24 * 24;
            p[0] = static_cast<std::uint8_t>(disc ? 230 : x * 2 + noise);
            p[1] = static_cast<std::uint8_t>(disc ? 40 : y * 2 + noise);
            p[2] = static_cast<std::uint8_t>((x ^ y) & 0x3f) + (x > 64 ? 128 : 0);
            p[3] = static_cast<std::uint8_t>(y < 16 ? 0 : y < 48 ? (y - 16) * 8 : 255);
        }
    return rgba;
}

std::uint64_t fnv1a(std::span<const std::uint8_t> data) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const auto b : data) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

std::vector<std::uint8_t> encode(const WebPConfig& config) {
    const auto rgba = picture();
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return {};
    pic.use_argb = 1;
    pic.width = W;
    pic.height = H;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;
    std::vector<std::uint8_t> out;
    if (WebPPictureImportRGBA(&pic, rgba.data(), W * 4) && WebPEncode(&config, &pic))
        out.assign(writer.mem, writer.mem + writer.size);
    WebPPictureFree(&pic);
    WebPMemoryWriterClear(&writer);
    return out;
}

struct Case {
    const char* name;
    int lossless;
    float quality;
    int method;      // lossy: method; lossless: the WebPConfigLosslessPreset level
    std::uint64_t expected;
};

}  // namespace

TEST(Determinism, EncodedBytesArePinned) {
    const Case cases[] = {
        {"lossy q50 m0", 0, 50, 0, 0x3fbae7dbd35518e5ull},
        {"lossy q75 m4", 0, 75, 4, 0x22522721157d9dd9ull},
        {"lossy q90 m6", 0, 90, 6, 0x8e8c480714b93d6aull},
        {"lossless level 0", 1, 0, 0, 0xb543b10b50599350ull},
        {"lossless level 6", 1, 0, 6, 0x67f05a16650abc6dull},
        {"lossless level 9", 1, 0, 9, 0x52a226853895b19eull},
    };
    for (const auto& c : cases) {
        WebPConfig config;
        ASSERT_TRUE(WebPConfigInit(&config));
        if (c.lossless) {
            ASSERT_TRUE(WebPConfigLosslessPreset(&config, c.method));
        } else {
            config.quality = c.quality;
            config.method = c.method;
        }
        const auto file = encode(config);
        ASSERT_FALSE(file.empty()) << c.name;
        const auto hash = fnv1a(file);
        std::println("{}: {} bytes, fnv1a 0x{:016x}", c.name, file.size(), hash);
        EXPECT_EQ(hash, c.expected) << c.name << ": 0x" << std::hex << hash;
    }
}

TEST(Determinism, DecodedPixelsArePinned) {
    WebPConfig config;
    ASSERT_TRUE(WebPConfigInit(&config));
    const auto file = encode(config);
    ASSERT_FALSE(file.empty());
    for (const int fancy : {1, 0}) {
        WebPDecoderConfig dc;
        ASSERT_TRUE(WebPInitDecoderConfig(&dc));
        dc.output.colorspace = MODE_RGBA;
        dc.options.no_fancy_upsampling = !fancy;
        ASSERT_EQ(WebPDecode(file.data(), file.size(), &dc), VP8_STATUS_OK);
        const auto hash = fnv1a({dc.output.u.RGBA.rgba, dc.output.u.RGBA.size});
        std::println("decoded, fancy upsampling {}: fnv1a 0x{:016x}", fancy, hash);
        EXPECT_EQ(hash, fancy ? 0xf4122fabaf7ed9abull : 0x9662fda975cec432ull) << "fancy " << fancy << ": 0x" << std::hex << hash;
        WebPFreeDecBuffer(&dc.output);
    }
}
