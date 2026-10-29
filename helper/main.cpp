// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The all-users virtual devices helper (ADR-0013). The system bus
// starts it as root when Reverblabr asks for the write action and polkit has
// let the user through; it writes one file and exits when idle.

#include "helper/systemdevicesaction.h"

#include <KAuth/ActionReply>
#include <KAuth/HelperSupport>
#include <QObject>
#include <sys/stat.h>

using namespace Qt::StringLiterals;

namespace rb::helper
{

class VirtualDevicesHelper : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    /// io.github.reverblabr.virtualdevices.write: KAuth calls the slot
    /// named after the action.
    KAuth::ActionReply write(const QVariantMap &arguments)
    {
        // Every user's PipeWire reads the file and its directory, whatever
        // umask the bus started us with.
        ::umask(022);
        if (const auto written = writeSystemDevices(arguments, u"/"_s); !written) {
            KAuth::ActionReply reply = KAuth::ActionReply::HelperErrorReply();
            reply.addData(kReasonKey, written.error());
            reply.setErrorDescription(written.error());
            return reply;
        }
        return KAuth::ActionReply::SuccessReply();
    }
};

} // namespace rb::helper

KAUTH_HELPER_MAIN("io.github.reverblabr.virtualdevices", rb::helper::VirtualDevicesHelper)

#include "main.moc"
