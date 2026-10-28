// libwebp's API through the module: the simple decoding and encoding API
// (RGBA in and out, lossy at a quality and lossless, with alpha), the
// advanced API (WebPConfig and WebPPicture with a memory writer, presets,
// method and threads, WebPDecoderConfig with scaling, flipping and
// colourspaces), incremental decoding, the error paths, the adapted static
// inline API, WebPData and sharpyuv — spelled as the headers document them.
// And determinism: the same bytes for the same picture and settings, run
// after run, with thread_level set or not (compat.libwebp is built without
// WEBP_USE_THREAD: the work stays on the calling thread). The bytes
// themselves, across targets, are pinned in tests/determinism_test.cpp.
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.libwebp;

using namespace mcpplibs::mwrap::libwebp;  // WebPConfigInit, WebPPictureInit … (the adapted static inline API)
namespace macros = mcpplibs::mwrap::libwebp::macros;

namespace {

// A picture with smooth gradients and alpha that is fully transparent, fully
// opaque and in between. (A saturated chroma checkerboard would do: VP8's
// 4:2:0 chroma at default settings smears it, as every libwebp does.)
std::vector<std::uint8_t> picture(int width, int height) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            auto* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            p[0] = static_cast<std::uint8_t>(x * 255 / (width - 1));
            p[1] = static_cast<std::uint8_t>(y * 255 / (height - 1));
            p[2] = static_cast<std::uint8_t>(255 - (x + y) * 255 / (width + height - 2));
            p[3] = static_cast<std::uint8_t>(x < width / 4 ? 0 : x < width / 2 ? (y * 255 / (height - 1)) : 255);
        }
    return rgba;
}

struct Freed {
    std::uint8_t* data = nullptr;
    ~Freed() { WebPFree(data); }
};

std::span<const std::uint8_t> bytes(const Freed& f, std::size_t size) { return {f.data, size}; }

// PSNR of the colour channels of the opaque pixels. (Where alpha is partial,
// the lossy encoder weights chroma by alpha, by design; where it is 0, the
// colour is not kept unless WebPConfig::exact is set.)
double psnr(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    double error = 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); i += 4) {
        if (a[i + 3] != 255) continue;
        for (int c = 0; c < 3; ++c) {
            const double d = double(a[i + c]) - double(b[i + c]);
            error += d * d;
            ++n;
        }
    }
    return error == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 * double(n) / error);
}

// Encodes with the advanced API into memory.
std::vector<std::uint8_t> encode(const WebPConfig& config, std::span<const std::uint8_t> rgba, int width, int height) {
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return {};
    pic.use_argb = 1;
    pic.width = width;
    pic.height = height;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;
    std::vector<std::uint8_t> out;
    if (WebPPictureImportRGBA(&pic, rgba.data(), width * 4) && WebPEncode(&config, &pic))
        out.assign(writer.mem, writer.mem + writer.size);
    WebPPictureFree(&pic);
    WebPMemoryWriterClear(&writer);
    return out;
}

// Equal where it matters without WebPConfig::exact: alpha everywhere, and
// the colour wherever alpha is not 0.
bool visible_equal(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i += 4)
        if (a[i + 3] != b[i + 3] || (a[i + 3] != 0 && !std::equal(a.data() + i, a.data() + i + 3, b.data() + i))) return false;
    return true;
}

// Lossless with WebPConfig::exact: every byte of every pixel survives.
std::vector<std::uint8_t> encode_exact(std::span<const std::uint8_t> rgba, int width, int height) {
    WebPConfig config;
    if (!WebPConfigInit(&config)) return {};
    config.lossless = 1;
    config.exact = 1;
    return encode(config, rgba, width, height);
}

constexpr int W = 96, H = 64;

}  // namespace

TEST(Versions, AreTheRelease) {
    EXPECT_EQ(WebPGetDecoderVersion(), 0x010500);
    EXPECT_EQ(WebPGetEncoderVersion(), 0x010500);
    EXPECT_EQ(SharpYuvGetVersion(), macros::SHARPYUV_VERSION);
    EXPECT_FALSE(macros::WEBP_ABI_IS_INCOMPATIBLE(0x0200, macros::WEBP_DECODER_ABI_VERSION));
    EXPECT_TRUE(macros::WEBP_ABI_IS_INCOMPATIBLE(0x0300, macros::WEBP_DECODER_ABI_VERSION));
}

TEST(Simple, LosslessRgbaRoundTripsExactly) {
    const auto rgba = picture(W, H);
    Freed encoded;
    const std::size_t size = WebPEncodeLosslessRGBA(rgba.data(), W, H, W * 4, &encoded.data);
    ASSERT_GT(size, 0u);
    const auto file = bytes(encoded, size);
    EXPECT_TRUE(std::ranges::equal(file.first(4), std::string_view("RIFF"), {}, {}, [](char c) { return std::uint8_t(c); }));

    int width = 0, height = 0;
    ASSERT_TRUE(WebPGetInfo(file.data(), file.size(), &width, &height));
    EXPECT_EQ(width, W);
    EXPECT_EQ(height, H);

    WebPBitstreamFeatures features;
    ASSERT_EQ(WebPGetFeatures(file.data(), file.size(), &features), VP8_STATUS_OK);
    EXPECT_EQ(features.width, W);
    EXPECT_TRUE(features.has_alpha);
    EXPECT_FALSE(features.has_animation);
    EXPECT_EQ(features.format, 2);  // lossless

    // The simple API leaves WebPConfig::exact off: the colour under alpha 0
    // may change, every visible pixel is exact.
    Freed decoded;
    decoded.data = WebPDecodeRGBA(file.data(), file.size(), &width, &height);
    ASSERT_NE(decoded.data, nullptr);
    EXPECT_TRUE(visible_equal(bytes(decoded, rgba.size()), rgba));
}

TEST(Simple, LossyRgbaAtAQuality) {
    const auto rgba = picture(W, H);
    std::size_t previous = 0;
    for (const float quality : {20.f, 60.f, 95.f}) {
        Freed encoded;
        const std::size_t size = WebPEncodeRGBA(rgba.data(), W, H, W * 4, quality, &encoded.data);
        ASSERT_GT(size, 0u);
        EXPECT_GT(size, previous) << "quality " << quality;  // a higher quality spends more bytes
        previous = size;

        WebPBitstreamFeatures features;
        ASSERT_EQ(WebPGetFeatures(encoded.data, size, &features), VP8_STATUS_OK);
        EXPECT_EQ(features.format, 1);  // lossy (VP8 with an ALPH chunk)
        EXPECT_TRUE(features.has_alpha);

        int width = 0, height = 0;
        Freed decoded;
        decoded.data = WebPDecodeRGBA(encoded.data, size, &width, &height);
        ASSERT_NE(decoded.data, nullptr);
        ASSERT_EQ(width, W);
        const auto out = bytes(decoded, rgba.size());
        EXPECT_GT(psnr(rgba, out), quality < 50 ? 28.0 : 33.0) << "quality " << quality;
        // Alpha is compressed losslessly by default (alpha_quality 100).
        for (std::size_t i = 3; i < rgba.size(); i += 4) ASSERT_EQ(out[i], rgba[i]) << "pixel " << i / 4;
    }
}

TEST(Simple, OtherLayouts) {
    const auto rgba = picture(W, H);
    std::vector<std::uint8_t> rgb, bgra(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgb.insert(rgb.end(), {rgba[i], rgba[i + 1], rgba[i + 2]});
        bgra[i] = rgba[i + 2], bgra[i + 1] = rgba[i + 1], bgra[i + 2] = rgba[i], bgra[i + 3] = rgba[i + 3];
    }
    Freed a, b;
    const std::size_t from_rgb = WebPEncodeLosslessRGB(rgb.data(), W, H, W * 3, &a.data);
    const std::size_t from_bgra = WebPEncodeLosslessBGRA(bgra.data(), W, H, W * 4, &b.data);
    ASSERT_GT(from_rgb, 0u);
    ASSERT_GT(from_bgra, 0u);
    int width, height;
    Freed c, d;
    c.data = WebPDecodeRGB(a.data, from_rgb, &width, &height);
    d.data = WebPDecodeBGRA(b.data, from_bgra, &width, &height);
    ASSERT_TRUE(c.data && d.data);
    EXPECT_TRUE(std::ranges::equal(bytes(c, rgb.size()), rgb));
    for (std::size_t i = 0; i < bgra.size(); i += 4)  // visible pixels exact
        ASSERT_TRUE(bgra[i + 3] == 0 ? d.data[i + 3] == 0 : std::equal(bgra.data() + i, bgra.data() + i + 4, d.data + i)) << i / 4;

    // Into a caller's buffer, with a stride of its own.
    const auto exact = encode_exact(rgba, W, H);
    ASSERT_FALSE(exact.empty());
    std::vector<std::uint8_t> into(static_cast<std::size_t>(H) * (W * 4 + 16));
    ASSERT_NE(WebPDecodeRGBAInto(exact.data(), exact.size(), into.data(), into.size(), W * 4 + 16), nullptr);
    for (int y = 0; y < H; ++y)
        ASSERT_TRUE(std::equal(rgba.begin() + y * W * 4, rgba.begin() + (y + 1) * W * 4, into.begin() + y * (W * 4 + 16)));
}

TEST(Advanced, ConfigPresetsAndValidation) {
    WebPConfig config;
    ASSERT_TRUE(WebPConfigInit(&config));
    EXPECT_EQ(config.quality, 75.f);
    EXPECT_EQ(config.method, 4);
    EXPECT_EQ(config.lossless, 0);
    EXPECT_TRUE(WebPValidateConfig(&config));
    ASSERT_TRUE(WebPConfigPreset(&config, WEBP_PRESET_PHOTO, 90.f));
    EXPECT_EQ(config.quality, 90.f);
    ASSERT_TRUE(WebPConfigLosslessPreset(&config, 9));
    EXPECT_EQ(config.lossless, 1);
    EXPECT_EQ(config.method, 6);
    config.method = 7;
    EXPECT_FALSE(WebPValidateConfig(&config));
    // The *Internal entry the adaptation calls, with the ABI version the header names.
    ASSERT_TRUE(WebPConfigInitInternal(&config, WEBP_PRESET_DEFAULT, 75.f, macros::WEBP_ENCODER_ABI_VERSION));
    EXPECT_TRUE(WebPValidateConfig(&config));
}

TEST(Advanced, EncodeLossyAndLosslessThroughAPicture) {
    const auto rgba = picture(W, H);
    WebPConfig config;
    ASSERT_TRUE(WebPConfigInit(&config));
    config.quality = 80;
    config.method = 6;
    config.alpha_quality = 100;
    const auto lossy = encode(config, rgba, W, H);
    ASSERT_FALSE(lossy.empty());

    config.lossless = 1;
    config.exact = 1;
    const auto lossless = encode(config, rgba, W, H);
    ASSERT_FALSE(lossless.empty());

    for (const auto* file : {&lossy, &lossless}) {
        WebPDecoderConfig dc;
        ASSERT_TRUE(WebPInitDecoderConfig(&dc));
        ASSERT_EQ(WebPGetFeatures(file->data(), file->size(), &dc.input), VP8_STATUS_OK);
        EXPECT_EQ(dc.input.format, file == &lossy ? 1 : 2);
        dc.output.colorspace = MODE_RGBA;
        ASSERT_EQ(WebPDecode(file->data(), file->size(), &dc), VP8_STATUS_OK);
        ASSERT_EQ(dc.output.width, W);
        const std::span<const std::uint8_t> out(dc.output.u.RGBA.rgba, dc.output.u.RGBA.size);
        ASSERT_EQ(out.size(), rgba.size());
        if (file == &lossless) EXPECT_TRUE(std::ranges::equal(out, rgba));
        else EXPECT_GT(psnr(rgba, out), 32.0);
        WebPFreeDecBuffer(&dc.output);
    }
}

TEST(Advanced, PictureStatsProgressAndDistortion) {
    const auto rgba = picture(W, H);
    WebPConfig config;
    ASSERT_TRUE(WebPConfigInit(&config));
    WebPPicture pic;
    ASSERT_TRUE(WebPPictureInit(&pic));
    pic.width = W;
    pic.height = H;
    pic.use_argb = 1;
    ASSERT_TRUE(WebPPictureImportRGBA(&pic, rgba.data(), W * 4));
    EXPECT_TRUE(WebPPictureHasTransparency(&pic));

    WebPAuxStats stats;
    pic.stats = &stats;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;
    static int last_percent;
    last_percent = -1;
    pic.progress_hook = [](int percent, const WebPPicture*) {
        last_percent = percent;
        return 1;
    };
    ASSERT_TRUE(WebPEncode(&config, &pic));
    EXPECT_GT(last_percent, 50);
    EXPECT_EQ(stats.coded_size, static_cast<int>(writer.size));
    EXPECT_GT(stats.PSNR[4], 25.f);  // the alpha plane's PSNR is stats.PSNR[4]

    // Distortion against a copy of itself is perfect.
    WebPPicture copy;
    ASSERT_TRUE(WebPPictureInit(&copy));
    ASSERT_TRUE(WebPPictureCopy(&pic, &copy));
    float result[5];
    ASSERT_TRUE(WebPPictureDistortion(&pic, &copy, 0, result));
    EXPECT_GE(result[4], 99.f);
    // A crop and a rescale.
    ASSERT_TRUE(WebPPictureCrop(&copy, 8, 8, 32, 16));
    EXPECT_EQ(copy.width, 32);
    ASSERT_TRUE(WebPPictureRescale(&copy, 16, 8));
    EXPECT_EQ(copy.height, 8);
    WebPPictureFree(&copy);
    WebPPictureFree(&pic);
    WebPMemoryWriterClear(&writer);
}

TEST(Advanced, AbortedByTheProgressHook) {
    const auto rgba = picture(W, H);
    WebPConfig config;
    ASSERT_TRUE(WebPConfigInit(&config));
    WebPPicture pic;
    ASSERT_TRUE(WebPPictureInit(&pic));
    pic.width = W;
    pic.height = H;
    ASSERT_TRUE(WebPPictureImportRGBA(&pic, rgba.data(), W * 4));
    pic.progress_hook = [](int, const WebPPicture*) { return 0; };
    EXPECT_FALSE(WebPEncode(&config, &pic));
    EXPECT_EQ(pic.error_code, VP8_ENC_ERROR_USER_ABORT);
    WebPPictureFree(&pic);
}

TEST(Advanced, BadDimensionIsAnError) {
    WebPConfig config;
    ASSERT_TRUE(WebPConfigInit(&config));
    WebPPicture pic;
    ASSERT_TRUE(WebPPictureInit(&pic));
    pic.width = macros::WEBP_MAX_DIMENSION + 1;
    pic.height = 1;
    ASSERT_TRUE(WebPPictureAlloc(&pic));  // the picture may be larger; the bitstream may not
    EXPECT_FALSE(WebPEncode(&config, &pic));
    EXPECT_EQ(pic.error_code, VP8_ENC_ERROR_BAD_DIMENSION);
    WebPPictureFree(&pic);
    // The simple API answers 0 bytes.
    std::uint8_t pixel[4] = {1, 2, 3, 4};
    std::uint8_t* out = nullptr;
    EXPECT_EQ(WebPEncodeRGBA(pixel, macros::WEBP_MAX_DIMENSION + 1, 1, 4, 75.f, &out), 0u);
    WebPFree(out);
}

TEST(Advanced, DecodeScaledFlippedAndInOtherColourspaces) {
    const auto rgba = picture(W, H);
    const auto file = encode_exact(rgba, W, H);
    ASSERT_FALSE(file.empty());
    const auto* const encoded_data = file.data();
    const std::size_t size = file.size();

    WebPDecoderConfig dc;
    ASSERT_TRUE(WebPInitDecoderConfig(&dc));
    dc.options.use_scaling = 1;
    dc.options.scaled_width = W / 2;
    dc.options.scaled_height = H / 2;
    dc.output.colorspace = MODE_rgbA;  // premultiplied
    ASSERT_EQ(WebPDecode(encoded_data, size, &dc), VP8_STATUS_OK);
    EXPECT_EQ(dc.output.width, W / 2);
    EXPECT_EQ(dc.output.height, H / 2);
    WebPFreeDecBuffer(&dc.output);

    ASSERT_TRUE(WebPInitDecoderConfig(&dc));
    dc.options.flip = 1;
    dc.output.colorspace = MODE_RGBA;
    ASSERT_EQ(WebPDecode(encoded_data, size, &dc), VP8_STATUS_OK);
    const auto* top = dc.output.u.RGBA.rgba;
    EXPECT_TRUE(std::equal(rgba.end() - W * 4, rgba.end(), top));  // the last row first
    WebPFreeDecBuffer(&dc.output);

    ASSERT_TRUE(WebPInitDecoderConfig(&dc));
    dc.output.colorspace = MODE_YUVA;
    ASSERT_EQ(WebPDecode(encoded_data, size, &dc), VP8_STATUS_OK);
    EXPECT_NE(dc.output.u.YUVA.a, nullptr);
    EXPECT_EQ(dc.output.u.YUVA.u_stride, (W + 1) / 2);
    WebPFreeDecBuffer(&dc.output);

    // Into a caller's buffer (is_external_memory).
    std::vector<std::uint8_t> mine(rgba.size());
    ASSERT_TRUE(WebPInitDecoderConfig(&dc));
    dc.output.colorspace = MODE_RGBA;
    dc.output.is_external_memory = 1;
    dc.output.u.RGBA.rgba = mine.data();
    dc.output.u.RGBA.stride = W * 4;
    dc.output.u.RGBA.size = mine.size();
    ASSERT_EQ(WebPDecode(encoded_data, size, &dc), VP8_STATUS_OK);
    EXPECT_EQ(mine, rgba);
    WebPFreeDecBuffer(&dc.output);
}

TEST(Incremental, AppendInPieces) {
    const auto rgba = picture(W, H);
    Freed encoded;
    const std::size_t size = WebPEncodeRGBA(rgba.data(), W, H, W * 4, 80.f, &encoded.data);
    ASSERT_GT(size, 0u);

    WebPDecBuffer buffer;
    ASSERT_TRUE(WebPInitDecBuffer(&buffer));
    buffer.colorspace = MODE_RGBA;
    WebPIDecoder* idec = WebPINewDecoder(&buffer);
    ASSERT_NE(idec, nullptr);
    VP8StatusCode status = VP8_STATUS_SUSPENDED;
    int suspended = 0;
    for (std::size_t at = 0; at < size; at += 97) {
        status = WebPIAppend(idec, encoded.data + at, std::min<std::size_t>(97, size - at));
        if (status == VP8_STATUS_SUSPENDED) ++suspended;
        else break;
    }
    EXPECT_EQ(status, VP8_STATUS_OK);
    EXPECT_GT(suspended, 3);
    int last_y = 0, width = 0, height = 0, stride = 0;
    EXPECT_NE(WebPIDecGetRGB(idec, &last_y, &width, &height, &stride), nullptr);
    EXPECT_EQ(last_y, H);
    WebPIDelete(idec);

    // The one-shot decode gives the same pixels.
    Freed whole;
    whole.data = WebPDecodeRGBA(encoded.data, size, &width, &height);
    EXPECT_TRUE(std::equal(buffer.u.RGBA.rgba, buffer.u.RGBA.rgba + rgba.size(), whole.data));
    WebPFreeDecBuffer(&buffer);

    // A YUVA decoder with its own buffers, read through the adapted WebPIDecGetYUV.
    idec = WebPINewYUVA(nullptr, 0, 0, nullptr, 0, 0, nullptr, 0, 0, nullptr, 0, 0);
    ASSERT_NE(idec, nullptr);
    ASSERT_EQ(WebPIUpdate(idec, encoded.data, size), VP8_STATUS_OK);
    std::uint8_t *u = nullptr, *v = nullptr;
    int uv_stride = 0;
    EXPECT_NE(WebPIDecGetYUV(idec, &last_y, &u, &v, &width, &height, &stride, &uv_stride), nullptr);
    EXPECT_TRUE(u && v);
    EXPECT_EQ(uv_stride, (W + 1) / 2);
    WebPIDelete(idec);
}

TEST(Errors, GarbageAndTruncation) {
    const std::array<std::uint8_t, 32> garbage{'R', 'I', 'F', 'F', 9, 9, 9, 9, 'W', 'E', 'B', 'X'};
    int width = -1, height = -1;
    EXPECT_FALSE(WebPGetInfo(garbage.data(), garbage.size(), &width, &height));
    EXPECT_EQ(WebPDecodeRGBA(garbage.data(), garbage.size(), &width, &height), nullptr);
    WebPBitstreamFeatures features;
    EXPECT_EQ(WebPGetFeatures(garbage.data(), garbage.size(), &features), VP8_STATUS_BITSTREAM_ERROR);

    const auto rgba = picture(W, H);
    Freed encoded;
    const std::size_t size = WebPEncodeRGBA(rgba.data(), W, H, W * 4, 50.f, &encoded.data);
    ASSERT_GT(size, 40u);
    EXPECT_EQ(WebPGetFeatures(encoded.data, 20, &features), VP8_STATUS_NOT_ENOUGH_DATA);
    EXPECT_EQ(WebPDecodeRGBA(encoded.data, size / 2, &width, &height), nullptr);
    WebPDecoderConfig dc;
    ASSERT_TRUE(WebPInitDecoderConfig(&dc));
    EXPECT_EQ(WebPDecode(encoded.data, size / 2, &dc), VP8_STATUS_NOT_ENOUGH_DATA);
    WebPFreeDecBuffer(&dc.output);
}

TEST(Inline, ModesAndData) {
    EXPECT_TRUE(WebPIsAlphaMode(MODE_RGBA));
    EXPECT_TRUE(WebPIsAlphaMode(MODE_rgbA));
    EXPECT_FALSE(WebPIsAlphaMode(MODE_RGB));
    EXPECT_TRUE(WebPIsPremultipliedMode(MODE_bgrA));
    EXPECT_FALSE(WebPIsPremultipliedMode(MODE_BGRA));
    EXPECT_TRUE(WebPIsRGBMode(MODE_ARGB));
    EXPECT_FALSE(WebPIsRGBMode(MODE_YUV));

    const std::array<std::uint8_t, 5> five{1, 2, 3, 4, 5};
    WebPData from{five.data(), five.size()}, copy;
    WebPDataInit(&copy);
    EXPECT_EQ(copy.bytes, nullptr);
    ASSERT_TRUE(WebPDataCopy(&from, &copy));
    EXPECT_NE(copy.bytes, from.bytes);
    EXPECT_TRUE(std::equal(copy.bytes, copy.bytes + copy.size, five.begin(), five.end()));
    WebPDataClear(&copy);  // WebPFree, and zeroed
    EXPECT_EQ(copy.size, 0u);

    // WebPMalloc / WebPFree, the library's allocator.
    void* block = WebPMalloc(64);
    EXPECT_NE(block, nullptr);
    WebPFree(block);
    EXPECT_EQ(ALPHA_FLAG, 0x10);
    EXPECT_EQ(ANIMATION_FLAG, 0x02);
}

TEST(Determinism, SameBytesEveryRunWithOrWithoutThreads) {
    const auto rgba = picture(W, H);
    for (const int lossless : {0, 1}) {
        WebPConfig config;
        ASSERT_TRUE(WebPConfigInit(&config));
        config.lossless = lossless;
        config.quality = 75;
        config.method = 4;
        const auto first = encode(config, rgba, W, H);
        ASSERT_FALSE(first.empty());
        EXPECT_EQ(encode(config, rgba, W, H), first);
        config.thread_level = 1;
        EXPECT_EQ(encode(config, rgba, W, H), first) << "thread_level 1, lossless " << lossless;
    }
}

TEST(SharpYuv, ConvertsAPicture) {
    const SharpYuvConversionMatrix* matrix = SharpYuvGetConversionMatrix(kSharpYuvMatrixWebp);
    ASSERT_NE(matrix, nullptr);
    SharpYuvOptions options;
    ASSERT_TRUE(SharpYuvOptionsInit(matrix, &options));
    EXPECT_EQ(options.transfer_type, kSharpYuvTransferFunctionSrgb);

    const auto rgba = picture(16, 16);
    std::vector<std::uint8_t> y(16 * 16), u(8 * 8), v(8 * 8);
    ASSERT_TRUE(SharpYuvConvertWithOptions(rgba.data(), rgba.data() + 1, rgba.data() + 2, 4, 16 * 4, 8, y.data(), 16,
                                           u.data(), 8, v.data(), 8, 8, 16, 16, &options));
    EXPECT_LT(y[0], y[15]);  // red rises to the right: so does luma

    SharpYuvConversionMatrix computed;
    const SharpYuvColorSpace bt709{0.2126f, 0.0722f, 8, kSharpYuvRangeLimited};
    SharpYuvComputeConversionMatrix(&bt709, &computed);
    const SharpYuvConversionMatrix* table = SharpYuvGetConversionMatrix(kSharpYuvMatrixRec709Limited);
    EXPECT_TRUE(std::ranges::equal(computed.rgb_to_y, table->rgb_to_y));
}
