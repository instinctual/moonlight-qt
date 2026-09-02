/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "streaming/video/plank2decodersource.h"

#include <memory>

std::shared_ptr<IPlankRetainedDecoderTarget>
createPlank2FfmpegSoftwareDecoderTarget();
