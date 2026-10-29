// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "helper/systemdevicesaction.h"

#include "pw/virtualdeviceconf.h"

#include <QStringList>

using namespace Qt::StringLiterals;

namespace rb::helper
{

namespace
{

[[nodiscard]] QString reason(pw::VirtualDeviceError error)
{
    const std::string_view name = pw::virtualDeviceErrorName(error);
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

} // namespace

std::expected<void, QString> writeSystemDevices(const QVariantMap &arguments, const QString &root)
{
    // Exactly these two, and nothing that could pass for a path or an
    // option: QVariantMap keeps its keys sorted.
    if (arguments.keys() != QStringList{u"devices"_s, u"previous"_s}) {
        return std::unexpected(QString(kBadRequest));
    }

    const auto devices = pw::virtualDevicesFromVariant(arguments.value(u"devices"_s), pw::VirtualDeviceScope::System);
    if (!devices) {
        return std::unexpected(reason(devices.error()));
    }
    const auto previous = pw::virtualDevicesFromVariant(arguments.value(u"previous"_s), pw::VirtualDeviceScope::System);
    if (!previous) {
        return std::unexpected(reason(previous.error()));
    }
    // The same check the engine makes on the user's own devices, so that
    // nothing reaches the file as root that would not reach it as the user.
    if (const auto valid = pw::validateVirtualDevices(*devices, pw::VirtualDeviceScope::System); !valid) {
        return std::unexpected(reason(valid.error()));
    }

    const pw::VirtualDeviceStore store(pw::VirtualDeviceScope::System, pw::VirtualDeviceStore::systemFile(root));
    const auto current = store.load();
    if (!current) {
        return std::unexpected(reason(current.error()));
    }
    if (*current != *previous) {
        return std::unexpected(QString(kChangedElsewhere));
    }
    if (const auto saved = store.save(*devices); !saved) {
        return std::unexpected(reason(saved.error()));
    }
    return {};
}

} // namespace rb::helper
