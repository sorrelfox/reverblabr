// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace rb::core
{

/// What a secret store answered.
struct SecretResult {
    /// Whether the store could be asked at all. A store that is locked, not
    /// there, or that the person refused to open is not.
    bool ok = false;
    /// The secret, when there is one; nothing when the store has none under
    /// that name, or after a write.
    std::optional<QString> secret;
    /// Why the store could not be asked, for the log and the error the
    /// person sees: "noSecretStore", "secretStoreDenied" or
    /// "secretStoreFailed".
    QString error;
};

/// **Where the engine keeps what must not be in a session file**: the
/// password a Broadcast block logs in to its server with (ADR-0026).
///
/// Asked, not called: the desktop's store may have to be unlocked first,
/// which can mean a password prompt, so every answer comes later, on the
/// thread the store was asked from, to \a done while \a context lives.
class SecretStore
{
public:
    using Done = std::function<void(const SecretResult &)>;

    virtual ~SecretStore() = default;

    /// The secret stored under \a key.
    virtual void read(const QString &key, QObject *context, Done done) = 0;
    /// Stores \a secret under \a key, or removes what is there when it is
    /// empty.
    virtual void write(const QString &key, const QString &secret, QObject *context, Done done) = 0;
};

/// The desktop's own store, through QtKeychain: KWallet on Plasma, and the
/// Secret Service (GNOME Keyring, KeePassXC) elsewhere. **Never a file of
/// its own**: QtKeychain's fallback to a plain-text settings file is off,
/// so with no store to use a password is refused rather than written down.
class KeychainSecretStore final : public SecretStore
{
public:
    void read(const QString &key, QObject *context, Done done) override;
    void write(const QString &key, const QString &secret, QObject *context, Done done) override;
};

/// **The store an engine keeps passwords in**: the desktop's, through
/// QtKeychain, in a build with the Broadcast block; in one without, a store
/// that refuses everything, since nothing there has a password to keep.
[[nodiscard]] std::unique_ptr<SecretStore> desktopSecretStore();

/// A store in memory, for tests and for an engine that should not touch the
/// desktop's: answers come later, as the real store's do.
class MemorySecretStore final : public SecretStore
{
public:
    void read(const QString &key, QObject *context, Done done) override;
    void write(const QString &key, const QString &secret, QObject *context, Done done) override;

    /// What it holds, for a test to look at.
    [[nodiscard]] const QHash<QString, QString> &secrets() const
    {
        return m_secrets;
    }

    /// Makes every call fail as a locked store does, or not.
    void setBroken(bool broken)
    {
        m_broken = broken;
    }

private:
    QHash<QString, QString> m_secrets;
    bool m_broken = false;
};

} // namespace rb::core
