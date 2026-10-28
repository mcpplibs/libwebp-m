# libwebp

> 通过 C++23 模块使用 WebP 图像编解码库 libwebp。

[English](README.md) · **简体中文** · [Upstream](https://github.com/webmproject/libwebp) · [Releases](https://github.com/mcpplibs/libwebp-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![libwebp](https://img.shields.io/badge/libwebp-BSD--3--Clause-blue.svg)](upstream/libwebp/COPYING)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/libwebp-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/libwebp-m/actions/workflows/ci.yml)

libwebp 1.5.0 解码和编码 WebP：有损（VP8）与无损（VP8L），支持 alpha，
静态图与动画。上游 C API 保持不变：`WebPGetInfo`、`WebPDecodeRGBA`、
`WebPEncodeRGBA`、`WebPEncodeLosslessRGBA`、配合 `WebPConfig` 与
`WebPPicture` 的 `WebPEncode`、`WebPAnimEncoder` 和 `WebPAnimDecoder`
都是头文件中的上游全局名。编解码器由 `compat.libwebp` 1.5.0 提供。头文件中的
`static inline` 函数（`WebPConfigInit`、`WebPPictureInit`、`WebPGetFeatures` …）
以同名同签名适配，宏映射为常量。每次 CI 都验证：写出的字节与读出的像素和上游自己的
`cwebp`、`dwebp` 完全一致，固定的哈希在所有目标上（包括 Web）都成立。

## 模块

- `import mcpplibs.mwrap.libwebp;`：解码（`webp/decode.h`）、编码（`webp/encode.h`）、`WebPData` 与动画枚举（`webp/mux_types.h`），并重新导出下面两个模块
- `import mcpplibs.mwrap.libwebp.sharpyuv;`：libsharpyuv，锐利的 RGB 到 YUV 转换（`SharpYuvConvert` …）
- `import mcpplibs.mwrap.libwebp.macros;`：`WEBP_MAX_DIMENSION`、各 ABI 版本、`WEBP_ABI_IS_INCOMPATIBLE` …，位于 `mcpplibs::mwrap::libwebp::macros`
- `import mcpplibs.mwrap.libwebp.demux;`：`WebPDemux`、`WebPAnimDecoder`，需要 `demux` 特性
- `import mcpplibs.mwrap.libwebp.mux;`：`WebPMux`、`WebPAnimEncoder`，需要 `mux` 特性

> 更多模块 API -> [用法与 API](docs/usage.md)

## 快速开始

```sh
mcpp new pictures --template mcpplibs.mwrap.libwebp@1.5.0
cd pictures
mcpp run
```

默认模板把一张带 alpha 的图片以三种质量有损编码、再无损编码，逐一解码回 RGBA，
打印大小与 PSNR。`animation` 模板（`--template mcpplibs.mwrap.libwebp@1.5.0:animation`）
用 `WebPAnimEncoder` 编码多帧，再用 `WebPAnimDecoder` 回放。

```toml
[dependencies]
"mcpplibs.mwrap.libwebp" = "1.5.0"
```

```cpp
import std;
import mcpplibs.mwrap.libwebp;

int main() {
    std::vector<std::uint8_t> rgba(64 * 64 * 4, 200);   // 浅灰色、不透明的方块
    std::uint8_t* webp = nullptr;
    const std::size_t size = WebPEncodeRGBA(rgba.data(), 64, 64, 64 * 4, 80.f, &webp);  // 有损，质量 80
    int width = 0, height = 0;
    std::uint8_t* pixels = WebPDecodeRGBA(webp, size, &width, &height);
    std::println("{} bytes, {} x {}", size, width, height);
    WebPFree(pixels);
    WebPFree(webp);
}
```

| 特性 | 作用 |
|---|---|
| `demux` | libwebpdemux（compat.libwebp 的 `src/demux`）与 `demux` 模块：`WebPDemux*`、`WebPAnimDecoder*` |
| `mux` | libwebpmux（compat.libwebp 的 `src/mux`）与 `mux` 模块：`WebPMux*`、`WebPAnimEncoder*` |

两者默认都不开启。读取动画 WebP 需要 `demux`，写出需要 `mux`。

## 编解码器

`compat.libwebp` 构建该版本的 `src/dec`、`src/enc`、`src/dsp`、`src/utils` 与
`sharpyuv`（开启特性时还有 `src/demux`、`src/mux`）：x86-64 上使用 SSE2，arm64 上使用
NEON，关闭 SSE4.1，且未定义 `WEBP_USE_THREAD`：`thread_level` 与 `use_threads`
可以设置，但在调用线程上执行。输出是确定的：同一图片与设置在 Linux、macOS、Windows
和 wasm32-emscripten 上，用 GCC 或 LLVM、dev 或 release，都得到相同的字节，并与上游
`cwebp` 的输出一致。细节与限制见 [docs/usage.md](docs/usage.md)。

## 示例

- [examples/webpcodec](examples/webpcodec)：把 WebP 解码为 PPM/PAM，把 PPM/PAM 编码为 WebP（有损或无损），
  打印文件特征，基于本模块编写；[tools/run_conformance.py](tools/run_conformance.py) 将它与上游的
  `cwebp`、`dwebp` 对比。

## 构建与测试

Linux（x86_64 GCC 与 LLVM、arm64 GCC）、macOS、Windows 和 wasm32-emscripten（node 下运行）
的 CI 构建模块，在 dev 与 release 配置下运行测试，包括开启与不开启特性；原生平台还运行导出审计
（[tools/check_exports.py](tools/check_exports.py)：生成的模块是最新的，每个声明都已导出，每个宏都已映射
或附理由排除），除 Windows 外还运行与上游工具的一致性对比。另有一个任务把两个模板当作已安装的包生成并运行。

## 许可证

包装层采用 [Apache-2.0](LICENSE)，作者 mcpp-community；libwebp 仍为 BSD-3-Clause，并附 Google 的专利授权
（[upstream/libwebp/COPYING](upstream/libwebp/COPYING)、[upstream/libwebp/PATENTS](upstream/libwebp/PATENTS)）。
