// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/paramdescriptor.h"
#include "dsp/paramvalue.h"

#include <QJsonValue>

#include <optional>

namespace rb::core
{

/// What a block's saved JSON means, read through the block's own parameter
/// list.
///
/// **A session keeps parameters as JSON on purpose.** A session written by a
/// later version may name a block this one does not have, and its settings
/// have to survive being loaded and saved again untouched. Turning them into
/// values needs the descriptor, because the same `"off"` is an enum choice
/// for one parameter and free text for another.
///
/// Nothing for a value that is absent, or that is not of the declared type —
/// a caller then keeps the default rather than being given nonsense.
[[nodiscard]] std::optional<dsp::ParamValue> valueFromJson(const dsp::ParamDescriptor &descriptor,
                                                           const QJsonValue &value);

/// A value on its way back into a session file, or out to the interface.
[[nodiscard]] QJsonValue valueToJson(const dsp::ParamValue &value);

} // namespace rb::core
