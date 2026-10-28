// webpcodec — WebP in and out, written against import mcpplibs.mwrap.libwebp.
//
//   webpcodec info   <in.webp>
//   webpcodec decode <in.webp> <out.ppm|out.pam> [-nofancy]
//   webpcodec encode <in.ppm|in.pam> <out.webp> [-q <quality>] [-m <method>] [-lossless] [-exact]
//
// decode writes what upstream's `dwebp -ppm` / `dwebp -pam` write: a binary
// PPM (P6, RGB) or a PAM (P7, RGB_ALPHA).
import std;
import mcpplibs.mwrap.libwebp;

using namespace mcpplibs::mwrap::libwebp;

namespace {

std::optional<std::vector<std::uint8_t>> read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool write(const std::string& path, std::string_view header, std::span<const std::uint8_t> body) {
    std::ofstream out(path, std::ios::binary);
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    return static_cast<bool>(out);
}

struct Image {
    int width = 0, height = 0;
    bool alpha = false;
    std::vector<std::uint8_t> rgba;  // always RGBA
};

// P6 (maxval 255) or P7 (RGB or RGB_ALPHA, maxval 255).
std::optional<Image> parse_pnm(const std::vector<std::uint8_t>& file) {
    std::size_t at = 0;
    auto token = [&]() -> std::string {
        while (at < file.size()) {
            if (file[at] == '#') while (at < file.size() && file[at] != '\n') ++at;
            else if (std::isspace(file[at])) ++at;
            else break;
        }
        std::string t;
        while (at < file.size() && !std::isspace(file[at])) t += static_cast<char>(file[at++]);
        return t;
    };
    Image image;
    int depth = 3, maxval = 0;
    const std::string magic = token();
    if (magic == "P6") {
        image.width = std::stoi(token());
        image.height = std::stoi(token());
        maxval = std::stoi(token());
    } else if (magic == "P7") {
        for (std::string key = token(); key != "ENDHDR" && !key.empty(); key = token()) {
            if (key == "WIDTH") image.width = std::stoi(token());
            else if (key == "HEIGHT") image.height = std::stoi(token());
            else if (key == "DEPTH") depth = std::stoi(token());
            else if (key == "MAXVAL") maxval = std::stoi(token());
            else if (key == "TUPLTYPE") token();
        }
    } else {
        return std::nullopt;
    }
    ++at;  // the single whitespace after the header
    if (maxval != 255 || (depth != 3 && depth != 4) || image.width <= 0 || image.height <= 0) return std::nullopt;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    if (file.size() - at < pixels * depth) return std::nullopt;
    image.alpha = depth == 4;
    image.rgba.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        std::copy_n(&file[at + i * depth], 3, &image.rgba[i * 4]);
        image.rgba[i * 4 + 3] = depth == 4 ? file[at + i * depth + 3] : 255;
    }
    return image;
}

int info(const std::vector<std::uint8_t>& file) {
    WebPBitstreamFeatures features;
    const VP8StatusCode status = WebPGetFeatures(file.data(), file.size(), &features);
    if (status != VP8_STATUS_OK) {
        std::println(std::cerr, "not a WebP file (status {})", static_cast<int>(status));
        return 1;
    }
    std::println("{} x {}, {}, {}{}", features.width, features.height,
                 features.format == 1 ? "lossy" : features.format == 2 ? "lossless" : "mixed",
                 features.has_alpha ? "with alpha" : "opaque", features.has_animation ? ", animated" : "");
    return 0;
}

int decode(const std::vector<std::uint8_t>& file, const std::string& out, bool fancy) {
    const bool pam = out.ends_with(".pam");
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) return 1;
    config.output.colorspace = pam ? MODE_RGBA : MODE_RGB;
    config.options.no_fancy_upsampling = !fancy;
    const VP8StatusCode status = WebPDecode(file.data(), file.size(), &config);
    if (status != VP8_STATUS_OK) {
        std::println(std::cerr, "decoding failed (status {})", static_cast<int>(status));
        return 1;
    }
    const auto& buffer = config.output;
    const std::string header = pam ? std::format("P7\nWIDTH {}\nHEIGHT {}\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",
                                                 buffer.width, buffer.height)
                                   : std::format("P6\n{} {}\n255\n", buffer.width, buffer.height);
    const bool ok = write(out, header, {buffer.u.RGBA.rgba, buffer.u.RGBA.size});
    WebPFreeDecBuffer(&config.output);
    return ok ? 0 : 1;
}

int encode(const Image& image, const std::string& out, float quality, int method, bool lossless, bool exact) {
    WebPConfig config;
    if (!WebPConfigInit(&config)) return 1;
    config.quality = quality;
    config.method = method;
    config.lossless = lossless;
    config.exact = exact;
    if (!WebPValidateConfig(&config)) {
        std::println(std::cerr, "invalid settings");
        return 1;
    }
    WebPPicture picture;
    if (!WebPPictureInit(&picture)) return 1;
    picture.width = image.width;
    picture.height = image.height;
    // ARGB samples for lossless, YUVA for lossy: as upstream's cwebp chooses,
    // so the same input and settings give cwebp's bytes.
    picture.use_argb = lossless;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    const bool imported = image.alpha ? WebPPictureImportRGBA(&picture, image.rgba.data(), image.width * 4)
                                      : WebPPictureImportRGBX(&picture, image.rgba.data(), image.width * 4);
    const bool ok = imported && WebPEncode(&config, &picture);
    if (!ok) std::println(std::cerr, "encoding failed (error {})", static_cast<int>(picture.error_code));
    const bool written = ok && write(out, {}, {writer.mem, writer.size});
    WebPPictureFree(&picture);
    WebPMemoryWriterClear(&writer);
    return written ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2) {
        std::println(std::cerr, "usage: webpcodec info <in.webp> | decode <in.webp> <out.ppm|.pam> [-nofancy] | "
                                "encode <in.ppm|.pam> <out.webp> [-q N] [-m N] [-lossless] [-exact]");
        return 2;
    }
    const auto file = read(args[1]);
    if (!file) {
        std::println(std::cerr, "cannot read {}", args[1]);
        return 1;
    }
    if (args[0] == "info") return info(*file);
    if (args.size() < 3) return 2;
    if (args[0] == "decode") return decode(*file, args[2], std::ranges::find(args, "-nofancy") == args.end());
    if (args[0] == "encode") {
        float quality = 75;
        int method = 4;
        bool lossless = false, exact = false;
        for (std::size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "-q" && i + 1 < args.size()) quality = std::stof(args[++i]);
            else if (args[i] == "-m" && i + 1 < args.size()) method = std::stoi(args[++i]);
            else if (args[i] == "-lossless") lossless = true;
            else if (args[i] == "-exact") exact = true;
        }
        const auto image = parse_pnm(*file);
        if (!image) {
            std::println(std::cerr, "{}: not a binary PPM or PAM with maxval 255", args[1]);
            return 1;
        }
        return encode(*image, args[2], quality, method, lossless, exact);
    }
    return 2;
}
