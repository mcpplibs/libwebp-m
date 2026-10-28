// The macro mappings as constants: usable in constant expressions, with the
// values of the headers; and at run time, the same versions as the library
// reports them.
import std;
import mcpplibs.mwrap.libwebp;

namespace m = mcpplibs::mwrap::libwebp::macros;

static_assert(m::WEBP_DECODER_ABI_VERSION == 0x0209 && m::WEBP_ENCODER_ABI_VERSION == 0x0210);
static_assert(m::WEBP_DEMUX_ABI_VERSION == 0x0107 && m::WEBP_MUX_ABI_VERSION == 0x0109);
static_assert(m::WEBP_MAX_DIMENSION == 16383);
static_assert(std::is_same_v<std::remove_cv_t<decltype(m::WEBP_MAX_DIMENSION)>, int>);
static_assert(!m::WEBP_ABI_IS_INCOMPATIBLE(0x0209, 0x0201) && m::WEBP_ABI_IS_INCOMPATIBLE(0x0209, 0x0309));
static_assert(m::SHARPYUV_VERSION_MAJOR == 0 && m::SHARPYUV_VERSION_MINOR == 4 && m::SHARPYUV_VERSION_PATCH == 1);
static_assert(m::SHARPYUV_VERSION == m::SHARPYUV_MAKE_VERSION(0, 4, 1) && m::SHARPYUV_VERSION == 0x00040001);

int main() {
    int failures = 0;
    for (auto [name, value, expected] : {std::tuple{"WebPGetDecoderVersion", WebPGetDecoderVersion(), 0x010500},
                                         {"WebPGetEncoderVersion", WebPGetEncoderVersion(), 0x010500},
                                         {"SharpYuvGetVersion", SharpYuvGetVersion(), m::SHARPYUV_VERSION}}) {
        const bool ok = value == expected;
        std::println("{:<24} 0x{:06x} {}", name, value, ok ? "ok" : "FAILED");
        failures += !ok;
    }
    // The ABI version the module's adaptations pass is the one the library accepts.
    WebPDecoderConfig config;
    const bool init = WebPInitDecoderConfigInternal(&config, m::WEBP_DECODER_ABI_VERSION) != 0;
    const bool refused = WebPInitDecoderConfigInternal(&config, m::WEBP_DECODER_ABI_VERSION + 0x100) == 0;
    std::println("WEBP_DECODER_ABI_VERSION accepted {}, a newer major refused {}", init ? "ok" : "FAILED",
                 refused ? "ok" : "FAILED");
    return failures + !init + !refused;
}
