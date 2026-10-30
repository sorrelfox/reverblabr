// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/connection.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace rb::stream
{

namespace
{

/// How long one wait lasts before \a cancel is looked at again.
constexpr std::chrono::milliseconds kSlice{50};

/// Waits for \a events on \a fd until \a deadline or \a cancel, a slice at
/// a time: the events that came, zero when time ran out, or an error.
[[nodiscard]] std::expected<short, StreamError>
waitFor(int fd, short events, std::chrono::steady_clock::time_point deadline, const std::atomic<bool> &cancel)
{
    for (;;) {
        if (cancel.load(std::memory_order_relaxed)) {
            return std::unexpected(StreamError::Closed);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return 0;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd entry{fd, events, 0};
        const int ready = ::poll(&entry, 1, static_cast<int>(std::min(left, kSlice).count()));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(StreamError::ConnectFailed);
        }
        if (ready > 0) {
            return entry.revents;
        }
    }
}

/// **Why the server's certificate was refused**, from what GnuTLS found
/// wrong with it. An authority this system does not trust comes first:
/// nothing else about a certificate matters until it does.
[[nodiscard]] StreamError certificateError(unsigned status) noexcept
{
    if ((status & (GNUTLS_CERT_SIGNER_NOT_FOUND | GNUTLS_CERT_SIGNER_NOT_CA)) != 0) {
        return StreamError::CertificateUntrusted;
    }
    if ((status & (GNUTLS_CERT_EXPIRED | GNUTLS_CERT_NOT_ACTIVATED)) != 0) {
        return StreamError::CertificateExpired;
    }
    if ((status & GNUTLS_CERT_UNEXPECTED_OWNER) != 0) {
        return StreamError::CertificateWrongHost;
    }
    return StreamError::CertificateInvalid;
}

/// **What a handshake that failed comes to.** Nothing in common with the
/// server (a version older than 1.2, no cipher this computer allows) stays
/// so, and is not tried again; anything else (an error the server sent
/// part way, something that is not TLS, such as a network's sign-in page,
/// or nothing in time) may be gone the next time.
[[nodiscard]] StreamError handshakeError(int result, gnutls_session_t session) noexcept
{
    switch (result) {
    case GNUTLS_E_UNSUPPORTED_VERSION_PACKET:
    case GNUTLS_E_NO_CIPHER_SUITES:
    case GNUTLS_E_UNKNOWN_CIPHER_SUITE:
    case GNUTLS_E_INSUFFICIENT_CREDENTIALS:
    case GNUTLS_E_NO_COMMON_KEY_SHARE:
        return StreamError::TlsFailed;
    case GNUTLS_E_TIMEDOUT:
        return StreamError::TimedOut;
    case GNUTLS_E_PREMATURE_TERMINATION:
    case GNUTLS_E_PULL_ERROR:
    case GNUTLS_E_PUSH_ERROR:
        return StreamError::Closed;
    case GNUTLS_E_FATAL_ALERT_RECEIVED:
        // The server's own word for the same mismatch.
        switch (gnutls_alert_get(session)) {
        case GNUTLS_A_PROTOCOL_VERSION:
        case GNUTLS_A_INSUFFICIENT_SECURITY:
        case GNUTLS_A_HANDSHAKE_FAILURE:
            return StreamError::TlsFailed;
        default:
            return StreamError::TlsHandshakeFailed;
        }
    default:
        return StreamError::TlsHandshakeFailed;
    }
}

/// Whether \a host is an address rather than a name: an address is not
/// sent as the server's name (RFC 6066 has no room for one), and is checked
/// against the addresses the certificate is made out to.
[[nodiscard]] bool isAddress(const std::string &host) noexcept
{
    in6_addr address{};
    return ::inet_pton(AF_INET, host.c_str(), &address) == 1 || ::inet_pton(AF_INET6, host.c_str(), &address) == 1;
}

} // namespace

std::expected<std::shared_ptr<const TlsTrust>, StreamError> TlsTrust::load(const TlsOptions &options)
{
    gnutls_certificate_credentials_t credentials = nullptr;
    if (gnutls_certificate_allocate_credentials(&credentials) < 0) {
        return std::unexpected(StreamError::TlsFailed);
    }
    std::shared_ptr<const TlsTrust> trust(new TlsTrust(credentials));
    // **The system's authorities, and nothing to switch the check off.** A
    // server whose certificate is the server's own can be trusted by adding
    // it to the system's store, as for any other program. The count is
    // what was read: none is a store that is missing, not a server to
    // distrust.
    const int read = options.trustFile.empty()
        ? gnutls_certificate_set_x509_system_trust(credentials)
        : gnutls_certificate_set_x509_trust_file(credentials, options.trustFile.c_str(), GNUTLS_X509_FMT_PEM);
    if (read <= 0) {
        return std::unexpected(StreamError::TrustStoreUnavailable);
    }
    return trust;
}

TlsTrust::~TlsTrust()
{
    gnutls_certificate_free_credentials(m_credentials);
}

/// A TLS session over the connection's socket, and the authorities it
/// checks the server against, kept for as long as the session uses them.
struct Connection::Tls {
    std::shared_ptr<const TlsTrust> trust;
    gnutls_session_t session = nullptr;

    Tls() = default;
    Tls(const Tls &) = delete;
    Tls &operator=(const Tls &) = delete;

    ~Tls()
    {
        if (session != nullptr) {
            gnutls_deinit(session);
        }
    }
};

void Connection::TlsDeleter::operator()(Tls *tls) const noexcept
{
    delete tls;
}

/// One name being looked up, shared by whoever asked and the thread that
/// asks the C library. **Whoever asked may give up and go**: the thread
/// finishes on its own, and the last of the two to let go frees the answer.
struct Connection::Lookup {
    std::mutex mutex;
    std::condition_variable done;
    bool finished = false;
    int result = EAI_FAIL;
    addrinfo *addresses = nullptr;

    ~Lookup()
    {
        if (addresses != nullptr) {
            ::freeaddrinfo(addresses);
        }
    }
};

std::expected<std::shared_ptr<const Connection::Lookup>, StreamError>
Connection::lookUp(const std::string &host,
                   std::uint16_t port,
                   std::chrono::steady_clock::time_point deadline,
                   const std::atomic<bool> &cancel)
{
    // **On a thread of its own, left behind if it takes too long.**
    // getaddrinfo() cannot be called off, and a resolver that does not
    // answer holds whoever called it for as long as its own timeouts say:
    // seconds, and a session that is stopping must not wait for it.
    auto lookup = std::make_shared<Lookup>();
    try {
        std::thread([lookup, host, service = std::to_string(port)] {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
            addrinfo *found = nullptr;
            const int result = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
            const std::lock_guard lock(lookup->mutex);
            lookup->result = result;
            lookup->addresses = found;
            lookup->finished = true;
            lookup->done.notify_all();
        }).detach();
    } catch (const std::system_error &) {
        return std::unexpected(StreamError::ResolveFailed);
    }
    std::unique_lock lock(lookup->mutex);
    while (!lookup->finished) {
        if (cancel.load(std::memory_order_relaxed)) {
            return std::unexpected(StreamError::Closed);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(StreamError::TimedOut);
        }
        lookup->done.wait_for(lock, kSlice);
    }
    if (lookup->result != 0 || lookup->addresses == nullptr) {
        return std::unexpected(StreamError::ResolveFailed);
    }
    return std::shared_ptr<const Lookup>(lookup);
}

std::expected<std::vector<std::string>, StreamError> Connection::resolve(const std::string &host,
                                                                         std::uint16_t port,
                                                                         std::chrono::milliseconds timeout,
                                                                         const std::atomic<bool> &cancel)
{
    if (host.empty()) {
        return std::unexpected(StreamError::NotConfigured);
    }
    std::expected<std::shared_ptr<const Lookup>, StreamError> looked =
        lookUp(host, port, std::chrono::steady_clock::now() + timeout, cancel);
    if (!looked) {
        return std::unexpected(looked.error());
    }
    std::vector<std::string> numbers;
    for (const addrinfo *address = (*looked)->addresses; address != nullptr; address = address->ai_next) {
        std::array<char, NI_MAXHOST> text{};
        if (::getnameinfo(address->ai_addr,
                          address->ai_addrlen,
                          text.data(),
                          static_cast<socklen_t>(text.size()),
                          nullptr,
                          0,
                          NI_NUMERICHOST)
                == 0
            && std::ranges::find(numbers, std::string{text.data()}) == numbers.end()) {
            numbers.emplace_back(text.data());
        }
    }
    if (numbers.empty()) {
        return std::unexpected(StreamError::ResolveFailed);
    }
    return numbers;
}

Connection::~Connection()
{
    close();
}

Connection::Connection(Connection &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1))
    , m_tls(std::move(other.m_tls))
    , m_tlsWaitsToWrite(std::exchange(other.m_tlsWaitsToWrite, false))
{
}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this != &other) {
        close();
        m_fd = std::exchange(other.m_fd, -1);
        m_tls = std::move(other.m_tls);
        m_tlsWaitsToWrite = std::exchange(other.m_tlsWaitsToWrite, false);
    }
    return *this;
}

void Connection::close() noexcept
{
    if (m_tls) {
        // **Said once, and not waited for**: a server that is told the
        // stream ends knows it was not cut off, and one that does not hear
        // it loses nothing.
        if (m_fd >= 0) {
            (void)gnutls_bye(m_tls->session, GNUTLS_SHUT_WR);
        }
        m_tls.reset();
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

std::expected<Connection, StreamError> Connection::open(const std::string &host,
                                                        std::uint16_t port,
                                                        std::chrono::milliseconds timeout,
                                                        const std::atomic<bool> &cancel)
{
    if (host.empty() || port == 0) {
        return std::unexpected(StreamError::NotConfigured);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::expected<std::shared_ptr<const Lookup>, StreamError> looked = lookUp(host, port, deadline, cancel);
    if (!looked) {
        return std::unexpected(looked.error());
    }
    const addrinfo *found = (*looked)->addresses;
    std::size_t count = 0;
    for (const addrinfo *address = found; address != nullptr; address = address->ai_next) {
        ++count;
    }

    StreamError last = StreamError::ConnectFailed;
    for (const addrinfo *address = found; address != nullptr; address = address->ai_next, --count) {
        // **Each address gets its share of the time left**, so a dead IPv6
        // route does not use up the time an IPv4 one would have answered in.
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            last = StreamError::TimedOut;
            break;
        }
        const auto share = (deadline - now) / static_cast<long>(std::max<std::size_t>(count, 1));
        const auto until = now + share;
        const int fd = ::socket(address->ai_family, address->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            continue;
        }
        Connection connection(fd);
        // The audio goes out as it is made, in small writes: Nagle's wait
        // for a fuller packet would only add to the delay.
        const int on = 1;
        (void)::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            return connection;
        }
        if (errno != EINPROGRESS) {
            continue;
        }
        const std::expected<short, StreamError> ready = waitFor(fd, POLLOUT, until, cancel);
        if (!ready) {
            return std::unexpected(ready.error());
        }
        if (*ready == 0) {
            last = StreamError::TimedOut;
            continue;
        }
        int error = 0;
        socklen_t length = sizeof error;
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0) {
            return connection;
        }
        last = StreamError::ConnectFailed;
    }
    return std::unexpected(last);
}

std::expected<void, StreamError> Connection::startTls(const std::string &host,
                                                      const TlsOptions &options,
                                                      std::chrono::milliseconds timeout,
                                                      const std::atomic<bool> &cancel)
{
    std::expected<std::shared_ptr<const TlsTrust>, StreamError> trust = TlsTrust::load(options);
    if (!trust) {
        return std::unexpected(trust.error());
    }
    return startTls(host, std::move(*trust), timeout, cancel);
}

std::expected<void, StreamError> Connection::startTls(const std::string &host,
                                                      std::shared_ptr<const TlsTrust> trust,
                                                      std::chrono::milliseconds timeout,
                                                      const std::atomic<bool> &cancel)
{
    if (m_fd < 0 || m_tls || !trust) {
        return std::unexpected(StreamError::Closed);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_ptr<Tls, TlsDeleter> tls(new Tls);
    tls->trust = std::move(trust);
    if (gnutls_init(&tls->session, GNUTLS_CLIENT | GNUTLS_NONBLOCK | GNUTLS_NO_SIGNAL) < 0) {
        return std::unexpected(StreamError::TlsFailed);
    }
    // The system's choice of ciphers, which its crypto policy sets, with
    // nothing older than TLS 1.2 whatever the policy allows, and no server
    // that cannot renegotiate safely.
    if (gnutls_set_default_priority_append(
            tls->session, "-VERS-SSL3.0:-VERS-TLS1.0:-VERS-TLS1.1:%SAFE_RENEGOTIATION", nullptr, 0)
            < 0
        || gnutls_credentials_set(tls->session, GNUTLS_CRD_CERTIFICATE, tls->trust->credentials()) < 0) {
        return std::unexpected(StreamError::TlsFailed);
    }
    if (!isAddress(host) && gnutls_server_name_set(tls->session, GNUTLS_NAME_DNS, host.data(), host.size()) < 0) {
        return std::unexpected(StreamError::TlsFailed);
    }
    // Checked in the handshake itself: the chain up to a trusted authority,
    // the dates, and the host's name or address.
    gnutls_session_set_verify_cert(tls->session, host.c_str(), 0);
    gnutls_transport_set_int(tls->session, m_fd);
    // **The deadline is ours**, and so is calling it off; GnuTLS is not to
    // keep a clock of its own.
    gnutls_handshake_set_timeout(tls->session, GNUTLS_INDEFINITE_TIMEOUT);

    for (;;) {
        const int result = gnutls_handshake(tls->session);
        if (result == GNUTLS_E_SUCCESS) {
            break;
        }
        if (result == GNUTLS_E_AGAIN || result == GNUTLS_E_INTERRUPTED) {
            // **Waited for a slice at a time**, which way the handshake
            // says it is waiting: a server that stops answering halfway is
            // let go of as soon as the stream stops.
            const short events = gnutls_record_get_direction(tls->session) == 1 ? POLLOUT : POLLIN;
            const std::expected<short, StreamError> ready = waitFor(m_fd, events, deadline, cancel);
            if (!ready) {
                return std::unexpected(ready.error());
            }
            if (*ready == 0) {
                return std::unexpected(StreamError::TimedOut);
            }
            continue;
        }
        if (gnutls_error_is_fatal(result) == 0) {
            continue;
        }
        if (result == GNUTLS_E_CERTIFICATE_VERIFICATION_ERROR || result == GNUTLS_E_CERTIFICATE_ERROR) {
            return std::unexpected(certificateError(gnutls_session_get_verify_cert_status(tls->session)));
        }
        return std::unexpected(handshakeError(result, tls->session));
    }
    m_tls = std::move(tls);
    m_tlsWaitsToWrite = false;
    return {};
}

std::expected<void, StreamError>
Connection::send(std::span<const char> bytes, std::chrono::milliseconds timeout, const std::atomic<bool> &cancel)
{
    if (m_fd < 0) {
        return std::unexpected(StreamError::Closed);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    if (m_tls) {
        while (!bytes.empty()) {
            // **The same bytes again after GNUTLS_E_AGAIN**, as GnuTLS
            // asks: part of a record may already be on its way.
            const ssize_t sent = gnutls_record_send(m_tls->session, bytes.data(), bytes.size());
            if (sent > 0) {
                bytes = bytes.subspan(static_cast<std::size_t>(sent));
                continue;
            }
            if (sent == GNUTLS_E_INTERRUPTED) {
                continue;
            }
            if (sent != GNUTLS_E_AGAIN) {
                return std::unexpected(StreamError::Closed);
            }
            const std::expected<short, StreamError> ready = waitFor(m_fd, POLLOUT, deadline, cancel);
            if (!ready) {
                return std::unexpected(ready.error());
            }
            if (*ready == 0) {
                return std::unexpected(StreamError::TimedOut);
            }
            if ((*ready & (POLLERR | POLLHUP)) != 0 && (*ready & POLLOUT) == 0) {
                return std::unexpected(StreamError::Closed);
            }
        }
        return {};
    }
    while (!bytes.empty()) {
        const ssize_t sent = ::send(m_fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent > 0) {
            bytes = bytes.subspan(static_cast<std::size_t>(sent));
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && errno == EAGAIN) {
            const std::expected<short, StreamError> ready = waitFor(m_fd, POLLOUT, deadline, cancel);
            if (!ready) {
                return std::unexpected(ready.error());
            }
            if (*ready == 0) {
                return std::unexpected(StreamError::TimedOut);
            }
            if ((*ready & (POLLERR | POLLHUP)) != 0 && (*ready & POLLOUT) == 0) {
                return std::unexpected(StreamError::Closed);
            }
            continue;
        }
        return std::unexpected(StreamError::Closed);
    }
    return {};
}

std::expected<bool, StreamError> Connection::receiveSome(std::string &received,
                                                         std::chrono::steady_clock::time_point deadline,
                                                         const std::atomic<bool> &cancel,
                                                         std::size_t maxBytes)
{
    if (m_fd < 0) {
        return false;
    }
    // What GnuTLS has read and not handed out yet is there without a wait;
    // otherwise the wait is the way the session last said it waits.
    if (!m_tls || gnutls_record_check_pending(m_tls->session) == 0) {
        const short events = m_tls && m_tlsWaitsToWrite ? POLLOUT : POLLIN;
        const std::expected<short, StreamError> ready = waitFor(m_fd, events, deadline, cancel);
        if (!ready) {
            return std::unexpected(ready.error());
        }
        if (*ready == 0) {
            return std::unexpected(StreamError::TimedOut);
        }
    }
    std::array<char, 2048> buffer{};
    if (m_tls) {
        const ssize_t got = gnutls_record_recv(m_tls->session, buffer.data(), buffer.size());
        m_tlsWaitsToWrite = false;
        if (got == GNUTLS_E_AGAIN || got == GNUTLS_E_INTERRUPTED) {
            m_tlsWaitsToWrite = gnutls_record_get_direction(m_tls->session) == 1;
            return true;
        }
        if (got < 0 && gnutls_error_is_fatal(static_cast<int>(got)) == 0) {
            return true;
        }
        // **Closed, cleanly or not**: a server that hangs up without saying
        // so in TLS has still hung up, and an answer it sent before is read
        // as far as it goes. Anything else fatal (a record that does not
        // decrypt, an alert) is not the end of an answer but an error.
        if (got == 0 || got == GNUTLS_E_PREMATURE_TERMINATION) {
            return false;
        }
        if (got < 0) {
            return std::unexpected(StreamError::Closed);
        }
        if (received.size() + static_cast<std::size_t>(got) > maxBytes) {
            return std::unexpected(StreamError::BadResponse);
        }
        received.append(buffer.data(), static_cast<std::size_t>(got));
        return true;
    }
    const ssize_t got = ::recv(m_fd, buffer.data(), buffer.size(), 0);
    if (got < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return true;
        }
        return false;
    }
    if (got == 0) {
        return false;
    }
    if (received.size() + static_cast<std::size_t>(got) > maxBytes) {
        return std::unexpected(StreamError::BadResponse);
    }
    received.append(buffer.data(), static_cast<std::size_t>(got));
    return true;
}

bool Connection::peerClosed()
{
    if (m_fd < 0) {
        return true;
    }
    pollfd entry{m_fd, POLLIN, 0};
    const bool pending = m_tls && gnutls_record_check_pending(m_tls->session) > 0;
    if (!pending && ::poll(&entry, 1, 0) <= 0) {
        return false;
    }
    if ((entry.revents & (POLLERR | POLLHUP)) != 0) {
        return true;
    }
    // Whatever the server says now is not wanted; reading it is how its
    // closing shows.
    std::array<char, 512> buffer{};
    if (m_tls) {
        // Over TLS what arrives may be the protocol's own (a TLS 1.3
        // server's session tickets), which GnuTLS takes and says nothing
        // of.
        const ssize_t got = gnutls_record_recv(m_tls->session, buffer.data(), buffer.size());
        return got == 0
            || (got < 0 && got != GNUTLS_E_AGAIN && got != GNUTLS_E_INTERRUPTED
                && gnutls_error_is_fatal(static_cast<int>(got)) != 0);
    }
    const ssize_t got = ::recv(m_fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
    return got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR);
}

} // namespace rb::stream
