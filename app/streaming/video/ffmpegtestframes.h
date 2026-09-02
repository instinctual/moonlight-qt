#pragma once

#include <cstddef>
#include <cstdint>

enum class PlankFfmpegTestFrameKind {
    H264,
    H264High8_422,
    H264High10_422,
    HevcMain,
    HevcMain10,
    Av1Main8,
    Av1Main10,
    H264High8_444,
    H264High10_444,
    HevcRext8_444,
    HevcRext8_444IdentityGbr,
    HevcRext10_444,
    HevcRext10_444IdentityGbr,
    Av1High8_444,
    Av1High10_444,
};

struct PlankFfmpegTestFrameView {
    const std::uint8_t* data;
    std::size_t size;
};

PlankFfmpegTestFrameView plankFfmpegTestFrame(PlankFfmpegTestFrameKind kind);
