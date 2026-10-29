// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/block.h"

#include "dsp/blockinfo.h"

namespace rb::dsp
{

std::string_view blockCategoryName(BlockCategory category) noexcept
{
    switch (category) {
    case BlockCategory::Sources:
        return "sources";
    case BlockCategory::Outputs:
        return "outputs";
    case BlockCategory::Effects:
        return "effects";
    case BlockCategory::Meters:
        return "meters";
    case BlockCategory::Advanced:
        return "advanced";
    case BlockCategory::Plugins:
        return "plugins";
    case BlockCategory::Hearing:
        return "hearing";
    }
    return "unknown";
}

std::string_view faceKindName(FaceKind face) noexcept
{
    switch (face) {
    case FaceKind::Meter:
        return "meter";
    case FaceKind::Value:
        return "value";
    case FaceKind::Icon:
        return "icon";
    case FaceKind::Text:
        return "text";
    case FaceKind::Bars:
        return "bars";
    case FaceKind::Curve:
        return "curve";
    case FaceKind::Vu:
        return "vu";
    }
    return "unknown";
}

std::string_view blockErrorName(BlockError error) noexcept
{
    switch (error) {
    case BlockError::UnknownType:
        return "unknownType";
    case BlockError::DuplicateType:
        return "duplicateType";
    case BlockError::UnknownParam:
        return "unknownParam";
    case BlockError::ParamTypeMismatch:
        return "paramTypeMismatch";
    case BlockError::ParamOutOfRange:
        return "paramOutOfRange";
    case BlockError::ParamReadOnly:
        return "paramReadOnly";
    case BlockError::InvalidDescriptor:
        return "invalidDescriptor";
    case BlockError::UnsupportedSpec:
        return "unsupportedSpec";
    }
    return "unknown";
}

} // namespace rb::dsp
