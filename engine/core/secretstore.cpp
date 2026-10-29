// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/secretstore.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>
#ifdef RB_HAVE_BROADCAST
#include <qt6keychain/keychain.h>
#endif

#include <utility>

using namespace Qt::StringLiterals;

#ifdef RB_HAVE_BROADCAST
Q_LOGGING_CATEGORY(lcSecrets, "reverblabr.secrets")
#endif

namespace rb::core
{

namespace
{

#ifdef RB_HAVE_BROADCAST
/// The store's folder for everything of ours: KWallet shows it by this
/// name.
constexpr auto kService = "Reverblabr";

[[nodiscard]] QString errorName(QKeychain::Error error)
{
    switch (error) {
    case QKeychain::NoBackendAvailable:
    case QKeychain::NotImplemented:
        return u"noSecretStore"_s;
    case QKeychain::AccessDenied:
    case QKeychain::AccessDeniedByUser:
        return u"secretStoreDenied"_s;
    default:
        return u"secretStoreFailed"_s;
    }
}

#endif

/// Sends \a result to \a done on the event loop, if \a context is still
/// there by then.
void answerLater(QObject *context, SecretStore::Done done, SecretResult result)
{
    QTimer::singleShot(0, context, [done = std::move(done), result = std::move(result)] { done(result); });
}

} // namespace

#ifdef RB_HAVE_BROADCAST
void KeychainSecretStore::read(const QString &key, QObject *context, Done done)
{
    auto *job = new QKeychain::ReadPasswordJob(QString::fromLatin1(kService));
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(key);
    QObject::connect(job, &QKeychain::Job::finished, context, [done = std::move(done)](QKeychain::Job *finished) {
        SecretResult result;
        if (finished->error() == QKeychain::NoError) {
            result.ok = true;
            result.secret = static_cast<QKeychain::ReadPasswordJob *>(finished)->textData();
        } else if (finished->error() == QKeychain::EntryNotFound) {
            result.ok = true;
        } else {
            result.error = errorName(finished->error());
            qCWarning(lcSecrets) << "the secret store would not be read:" << finished->errorString();
        }
        done(result);
    });
    job->start();
}

void KeychainSecretStore::write(const QString &key, const QString &secret, QObject *context, Done done)
{
    QKeychain::Job *job = nullptr;
    if (secret.isEmpty()) {
        auto *remove = new QKeychain::DeletePasswordJob(QString::fromLatin1(kService));
        remove->setKey(key);
        job = remove;
    } else {
        auto *store = new QKeychain::WritePasswordJob(QString::fromLatin1(kService));
        store->setKey(key);
        store->setTextData(secret);
        job = store;
    }
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    const bool removing = secret.isEmpty();
    QObject::connect(
        job, &QKeychain::Job::finished, context, [done = std::move(done), removing](QKeychain::Job *finished) {
            SecretResult result;
            // Nothing to remove is what removing was for.
            if (finished->error() == QKeychain::NoError
                || (removing && finished->error() == QKeychain::EntryNotFound)) {
                result.ok = true;
            } else {
                result.error = errorName(finished->error());
                qCWarning(lcSecrets) << "the secret store would not be written:" << finished->errorString();
            }
            done(result);
        });
    job->start();
}

#endif

std::unique_ptr<SecretStore> desktopSecretStore()
{
#ifdef RB_HAVE_BROADCAST
    return std::make_unique<KeychainSecretStore>();
#else
    auto refusing = std::make_unique<MemorySecretStore>();
    refusing->setBroken(true);
    return refusing;
#endif
}

void MemorySecretStore::read(const QString &key, QObject *context, Done done)
{
    SecretResult result;
    result.ok = !m_broken;
    if (m_broken) {
        result.error = u"noSecretStore"_s;
    } else if (m_secrets.contains(key)) {
        result.secret = m_secrets.value(key);
    }
    answerLater(context, std::move(done), std::move(result));
}

void MemorySecretStore::write(const QString &key, const QString &secret, QObject *context, Done done)
{
    SecretResult result;
    result.ok = !m_broken;
    if (m_broken) {
        result.error = u"noSecretStore"_s;
    } else if (secret.isEmpty()) {
        m_secrets.remove(key);
    } else {
        m_secrets.insert(key, secret);
    }
    answerLater(context, std::move(done), std::move(result));
}

} // namespace rb::core
