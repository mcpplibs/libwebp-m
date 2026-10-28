// Animated WebP and the container through the modules, with the demux and
// mux features: WebPAnimEncoder (lossless and lossy frames, timestamps, the
// loop count and background), WebPAnimDecoder (full canvases with their
// timestamps, reset, every supported colour mode), WebPDemux (frames,
// features, chunks), WebPMux (ICC and EXIF chunks on a still image, an
// animation assembled frame by frame), the error paths; and the assembled
// animation's bytes, pinned like tests/determinism_test.cpp's.
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.libwebp.demux;  // re-exports mcpplibs.mwrap.libwebp
import mcpplibs.mwrap.libwebp.mux;

using namespace mcpplibs::mwrap::libwebp;  // the adapted static inline API
namespace macros = mcpplibs::mwrap::libwebp::macros;

namespace {

constexpr int W = 64, H = 48, Frames = 4;

// Frame f: a square moving over a gradient, opaque except a translucent band.
std::vector<std::uint8_t> frame(int f) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            auto* p = &rgba[(static_cast<std::size_t>(y) * W + x) * 4];
            const bool square = x >= 8 + f * 10 && x < 24 + f * 10 && y >= 12 && y < 28;
            p[0] = static_cast<std::uint8_t>(square ? 250 : x * 4);
            p[1] = static_cast<std::uint8_t>(square ? 30 : y * 5);
            p[2] = static_cast<std::uint8_t>(square ? 60 : 128);
            p[3] = static_cast<std::uint8_t>(y >= 40 ? 128 : 255);
        }
    return rgba;
}

std::uint64_t fnv1a(std::span<const std::uint8_t> data) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const auto b : data) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

std::vector<std::uint8_t> animate(bool lossless, float quality) {
    WebPAnimEncoderOptions options;
    if (!WebPAnimEncoderOptionsInit(&options)) return {};
    options.anim_params.loop_count = 3;
    options.anim_params.bgcolor = 0xffffffffu;  // opaque white (blue, green, red, alpha from the top byte)
    WebPAnimEncoder* encoder = WebPAnimEncoderNew(W, H, &options);
    if (!encoder) return {};
    WebPConfig config;
    if (!WebPConfigInit(&config)) return {};
    config.lossless = lossless;
    config.quality = quality;
    config.exact = 1;
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return {};
    pic.use_argb = 1;
    pic.width = W;
    pic.height = H;
    bool ok = true;
    for (int f = 0; f < Frames && ok; ++f) {
        const auto rgba = frame(f);
        ok = WebPPictureImportRGBA(&pic, rgba.data(), W * 4) && WebPAnimEncoderAdd(encoder, &pic, f * 100, &config);
    }
    WebPPictureFree(&pic);
    ok = ok && WebPAnimEncoderAdd(encoder, nullptr, Frames * 100, nullptr);  // the last frame's end
    WebPData out;
    WebPDataInit(&out);
    ok = ok && WebPAnimEncoderAssemble(encoder, &out);
    WebPAnimEncoderDelete(encoder);
    std::vector<std::uint8_t> file;
    if (ok) file.assign(out.bytes, out.bytes + out.size);
    WebPDataClear(&out);
    return file;
}

}  // namespace

TEST(Anim, Versions) {
    EXPECT_EQ(WebPGetDemuxVersion(), 0x010500);
    EXPECT_EQ(WebPGetMuxVersion(), 0x010500);
    EXPECT_EQ(macros::WEBP_DEMUX_ABI_VERSION, 0x0107);
    EXPECT_EQ(macros::WEBP_MUX_ABI_VERSION, 0x0109);
}

TEST(Anim, LosslessRoundTripsEveryFrame) {
    const auto file = animate(true, 75);
    ASSERT_FALSE(file.empty());
    WebPBitstreamFeatures features;
    ASSERT_EQ(WebPGetFeatures(file.data(), file.size(), &features), VP8_STATUS_OK);
    EXPECT_TRUE(features.has_animation);

    const WebPData data{file.data(), file.size()};
    WebPAnimDecoderOptions options;
    ASSERT_TRUE(WebPAnimDecoderOptionsInit(&options));
    options.color_mode = MODE_RGBA;
    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
    ASSERT_NE(decoder, nullptr);
    WebPAnimInfo info;
    ASSERT_TRUE(WebPAnimDecoderGetInfo(decoder, &info));
    EXPECT_EQ(info.canvas_width, static_cast<std::uint32_t>(W));
    EXPECT_EQ(info.canvas_height, static_cast<std::uint32_t>(H));
    EXPECT_EQ(info.frame_count, static_cast<std::uint32_t>(Frames));
    EXPECT_EQ(info.loop_count, 3u);

    for (int pass = 0; pass < 2; ++pass) {  // and again after a reset
        int f = 0;
        while (WebPAnimDecoderHasMoreFrames(decoder)) {
            std::uint8_t* canvas = nullptr;
            int timestamp = 0;
            ASSERT_TRUE(WebPAnimDecoderGetNext(decoder, &canvas, &timestamp));
            EXPECT_EQ(timestamp, (f + 1) * 100);  // the frame's end
            const auto expected = frame(f);
            EXPECT_TRUE(std::equal(expected.begin(), expected.end(), canvas)) << "frame " << f;
            ++f;
        }
        EXPECT_EQ(f, Frames);
        WebPAnimDecoderReset(decoder);
    }
    const WebPDemuxer* demux = WebPAnimDecoderGetDemuxer(decoder);
    EXPECT_EQ(WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT), static_cast<std::uint32_t>(Frames));
    WebPAnimDecoderDelete(decoder);
}

TEST(Anim, LossyCloseAndEveryColourMode) {
    const auto file = animate(false, 80);
    ASSERT_FALSE(file.empty());
    const WebPData data{file.data(), file.size()};
    for (const auto mode : {MODE_RGBA, MODE_BGRA, MODE_rgbA, MODE_bgrA}) {
        WebPAnimDecoderOptions options;
        ASSERT_TRUE(WebPAnimDecoderOptionsInit(&options));
        options.color_mode = mode;
        options.use_threads = 1;
        WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
        ASSERT_NE(decoder, nullptr) << mode;
        std::uint8_t* canvas = nullptr;
        int timestamp = 0;
        ASSERT_TRUE(WebPAnimDecoderGetNext(decoder, &canvas, &timestamp));
        if (mode == MODE_RGBA) {
            const auto expected = frame(0);
            double error = 0;
            for (std::size_t i = 0; i < expected.size(); ++i) error += std::abs(int(expected[i]) - int(canvas[i]));
            EXPECT_LT(error / double(expected.size()), 4.0);  // mean absolute error per channel
        }
        WebPAnimDecoderDelete(decoder);
    }
    // A colour mode the animation decoder does not support.
    WebPAnimDecoderOptions options;
    ASSERT_TRUE(WebPAnimDecoderOptionsInit(&options));
    options.color_mode = MODE_RGB;
    EXPECT_EQ(WebPAnimDecoderNew(&data, &options), nullptr);
}

TEST(Anim, DemuxFrames) {
    const auto file = animate(true, 75);
    ASSERT_FALSE(file.empty());
    const WebPData data{file.data(), file.size()};
    WebPDemuxer* demux = WebPDemux(&data);
    ASSERT_NE(demux, nullptr);
    EXPECT_EQ(WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH), static_cast<std::uint32_t>(W));
    EXPECT_EQ(WebPDemuxGetI(demux, WEBP_FF_LOOP_COUNT), 3u);
    EXPECT_TRUE(WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS) & ANIMATION_FLAG);
    WebPIterator it;
    ASSERT_TRUE(WebPDemuxGetFrame(demux, 1, &it));
    int frames = 0, total = 0;
    do {
        ++frames;
        total += it.duration;
        EXPECT_TRUE(it.complete);
        EXPECT_GT(it.fragment.size, 0u);
    } while (WebPDemuxNextFrame(&it));
    WebPDemuxReleaseIterator(&it);
    EXPECT_EQ(frames, Frames);
    EXPECT_EQ(total, Frames * 100);
    WebPDemuxDelete(demux);

    // Partial data: the header parses, the frames are not all there yet.
    const WebPData half{file.data(), file.size() / 2};
    WebPDemuxState state;
    demux = WebPDemuxPartial(&half, &state);
    ASSERT_NE(demux, nullptr);
    EXPECT_TRUE(state == WEBP_DEMUX_PARSED_HEADER || state == WEBP_DEMUX_PARSING_HEADER);
    WebPDemuxDelete(demux);
}

TEST(Mux, ChunksOnAStillImage) {
    const auto rgba = frame(0);
    std::uint8_t* still = nullptr;
    const std::size_t size = WebPEncodeLosslessRGBA(rgba.data(), W, H, W * 4, &still);
    ASSERT_GT(size, 0u);
    const WebPData image{still, size};
    WebPMux* mux = WebPMuxCreate(&image, 1);
    ASSERT_NE(mux, nullptr);
    WebPFree(still);

    using namespace std::literals;
    const std::string icc = "not really an ICC profile", exif = "Exif\0\0MM"s;
    const WebPData icc_data{reinterpret_cast<const std::uint8_t*>(icc.data()), icc.size()};
    const WebPData exif_data{reinterpret_cast<const std::uint8_t*>(exif.data()), exif.size()};
    ASSERT_EQ(WebPMuxSetChunk(mux, "ICCP", &icc_data, 1), WEBP_MUX_OK);
    ASSERT_EQ(WebPMuxSetChunk(mux, "EXIF", &exif_data, 1), WEBP_MUX_OK);
    WebPData assembled;
    WebPDataInit(&assembled);
    ASSERT_EQ(WebPMuxAssemble(mux, &assembled), WEBP_MUX_OK);
    WebPMuxDelete(mux);

    // Read back through the demuxer, and the image still decodes.
    WebPDemuxer* demux = WebPDemux(&assembled);
    ASSERT_NE(demux, nullptr);
    WebPChunkIterator chunk;
    ASSERT_TRUE(WebPDemuxGetChunk(demux, "ICCP", 1, &chunk));
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(chunk.chunk.bytes), chunk.chunk.size), icc);
    WebPDemuxReleaseChunkIterator(&chunk);
    EXPECT_FALSE(WebPDemuxGetChunk(demux, "XMP ", 1, &chunk));
    WebPDemuxDelete(demux);
    int width = 0, height = 0;
    std::uint8_t* pixels = WebPDecodeRGBA(assembled.bytes, assembled.size, &width, &height);
    ASSERT_NE(pixels, nullptr);
    EXPECT_TRUE(std::equal(rgba.begin(), rgba.end(), pixels));
    WebPFree(pixels);

    mux = WebPMuxCreate(&assembled, 0);
    ASSERT_NE(mux, nullptr);
    std::uint32_t flags = 0;  // from the VP8X chunk the assembly wrote
    ASSERT_EQ(WebPMuxGetFeatures(mux, &flags), WEBP_MUX_OK);
    EXPECT_TRUE(flags & ICCP_FLAG);
    EXPECT_TRUE(flags & EXIF_FLAG);
    EXPECT_TRUE(flags & ALPHA_FLAG);
    WebPData got;
    ASSERT_EQ(WebPMuxGetChunk(mux, "EXIF", &got), WEBP_MUX_OK);
    EXPECT_EQ(got.size, exif.size());
    EXPECT_EQ(WebPMuxDeleteChunk(mux, "EXIF"), WEBP_MUX_OK);
    EXPECT_EQ(WebPMuxGetChunk(mux, "EXIF", &got), WEBP_MUX_NOT_FOUND);
    WebPMuxDelete(mux);
    WebPDataClear(&assembled);
}

TEST(Mux, AnAnimationFrameByFrame) {
    WebPMux* mux = WebPMuxNew();
    ASSERT_NE(mux, nullptr);
    const WebPMuxAnimParams params{0xffffffffu, 0};
    ASSERT_EQ(WebPMuxSetAnimationParams(mux, &params), WEBP_MUX_OK);
    std::vector<std::uint8_t*> stills;
    for (int f = 0; f < 2; ++f) {
        const auto rgba = frame(f);
        std::uint8_t* still = nullptr;
        const std::size_t size = WebPEncodeLosslessRGBA(rgba.data(), W, H, W * 4, &still);
        ASSERT_GT(size, 0u);
        stills.push_back(still);
        WebPMuxFrameInfo info{};
        info.bitstream = {still, size};
        info.duration = 250;
        info.id = WEBP_CHUNK_ANMF;
        info.dispose_method = WEBP_MUX_DISPOSE_NONE;
        info.blend_method = WEBP_MUX_NO_BLEND;
        ASSERT_EQ(WebPMuxPushFrame(mux, &info, 1), WEBP_MUX_OK);
    }
    for (auto* still : stills) WebPFree(still);
    int frames = 0;
    ASSERT_EQ(WebPMuxNumChunks(mux, WEBP_CHUNK_ANMF, &frames), WEBP_MUX_OK);
    EXPECT_EQ(frames, 2);
    WebPData out;
    WebPDataInit(&out);
    ASSERT_EQ(WebPMuxAssemble(mux, &out), WEBP_MUX_OK);
    WebPMuxDelete(mux);
    WebPDemuxer* demux = WebPDemux(&out);
    ASSERT_NE(demux, nullptr);
    EXPECT_EQ(WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT), 2u);
    EXPECT_EQ(WebPDemuxGetI(demux, WEBP_FF_BACKGROUND_COLOR), 0xffffffffu);
    WebPDemuxDelete(demux);
    WebPDataClear(&out);
}

TEST(Anim, Errors) {
    const std::array<std::uint8_t, 16> garbage{'R', 'I', 'F', 'F', 8, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', 'Q'};
    const WebPData data{garbage.data(), garbage.size()};
    EXPECT_EQ(WebPDemux(&data), nullptr);
    EXPECT_EQ(WebPAnimDecoderNew(&data, nullptr), nullptr);
    EXPECT_EQ(WebPMuxCreate(&data, 0), nullptr);

    WebPAnimEncoder* encoder = WebPAnimEncoderNew(W, H, nullptr);
    ASSERT_NE(encoder, nullptr);
    WebPPicture pic;
    ASSERT_TRUE(WebPPictureInit(&pic));
    pic.width = W + 1;  // not the canvas
    pic.height = H;
    ASSERT_TRUE(WebPPictureAlloc(&pic));
    EXPECT_FALSE(WebPAnimEncoderAdd(encoder, &pic, 0, nullptr));
    EXPECT_NE(std::string_view(WebPAnimEncoderGetError(encoder)), "");
    WebPPictureFree(&pic);
    WebPAnimEncoderDelete(encoder);
}

TEST(Anim, AssembledBytesArePinned) {
    const auto lossless = animate(true, 75), lossy = animate(false, 75);
    ASSERT_FALSE(lossless.empty());
    ASSERT_FALSE(lossy.empty());
    std::println("animation lossless: {} bytes, fnv1a 0x{:016x}", lossless.size(), fnv1a(lossless));
    std::println("animation lossy q75: {} bytes, fnv1a 0x{:016x}", lossy.size(), fnv1a(lossy));
    EXPECT_EQ(animate(true, 75), lossless);
    EXPECT_EQ(fnv1a(lossless), 0x8acfc1377bc9e0c9ull) << std::hex << fnv1a(lossless);
    EXPECT_EQ(fnv1a(lossy), 0x507c1a778261343cull) << std::hex << fnv1a(lossy);
}
