// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// TLS for the stream's tests, made in the test: a certificate authority of
// its own and certificates it signs (for a name, an address, already
// expired), and a server that takes TLS on a port of its own and passes
// what comes through to a plain server behind it, as a TLS terminator in
// front of Icecast does. Nothing here is the system's: the client is told
// to trust the test's authority in place of the system's.

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rb::test
{

/// A key and the certificate made for it, in PEM.
struct Certificate {
    std::string certificatePem;
    std::string keyPem;
};

/// **A certificate authority made for one test run**, and the server
/// certificates it signs.
class TestAuthority
{
public:
    /// What a server certificate is made out to, and when it is good.
    struct Server {
        std::vector<std::string> names;
        std::vector<std::array<unsigned char, 4>> addresses;
        /// Seconds from now: the start, and the end.
        long from = -3600;
        long until = 86400;
    };

    explicit TestAuthority(const std::string &name = "Reverblabr Test Authority")
    {
        gnutls_x509_privkey_init(&m_key);
        gnutls_x509_privkey_generate(m_key, GNUTLS_PK_ECDSA, GNUTLS_CURVE_TO_BITS(GNUTLS_ECC_CURVE_SECP256R1), 0);
        gnutls_x509_crt_init(&m_certificate);
        fill(m_certificate, m_key, name, -3600, 86400 * 30);
        gnutls_x509_crt_set_basic_constraints(m_certificate, 1, -1);
        gnutls_x509_crt_set_key_usage(m_certificate, GNUTLS_KEY_KEY_CERT_SIGN | GNUTLS_KEY_CRL_SIGN);
        sign(m_certificate, m_certificate, m_key);
        m_pem = exported(m_certificate);
    }

    ~TestAuthority()
    {
        gnutls_x509_crt_deinit(m_certificate);
        gnutls_x509_privkey_deinit(m_key);
        std::error_code ignored;
        std::filesystem::remove(m_file, ignored);
    }

    TestAuthority(const TestAuthority &) = delete;
    TestAuthority &operator=(const TestAuthority &) = delete;

    /// The authority's certificate, in a file of its own: what a client is
    /// told to trust.
    [[nodiscard]] std::string trustFile()
    {
        if (m_file.empty()) {
            static std::atomic<int> count{0};
            m_file = std::filesystem::temp_directory_path()
                / ("reverblabr-test-ca-" + std::to_string(::getpid()) + "-" + std::to_string(count++) + ".pem");
            std::ofstream(m_file) << m_pem;
        }
        return m_file.string();
    }

    [[nodiscard]] Certificate issue(const Server &server) const
    {
        gnutls_x509_privkey_t key = nullptr;
        gnutls_x509_privkey_init(&key);
        gnutls_x509_privkey_generate(key, GNUTLS_PK_ECDSA, GNUTLS_CURVE_TO_BITS(GNUTLS_ECC_CURVE_SECP256R1), 0);
        gnutls_x509_crt_t certificate = nullptr;
        gnutls_x509_crt_init(&certificate);
        fill(certificate, key, server.names.empty() ? "server" : server.names.front(), server.from, server.until);
        gnutls_x509_crt_set_basic_constraints(certificate, 0, -1);
        gnutls_x509_crt_set_key_usage(certificate, GNUTLS_KEY_DIGITAL_SIGNATURE);
        gnutls_x509_crt_set_key_purpose_oid(certificate, GNUTLS_KP_TLS_WWW_SERVER, 0);
        for (const std::string &name : server.names) {
            gnutls_x509_crt_set_subject_alt_name(
                certificate, GNUTLS_SAN_DNSNAME, name.data(), static_cast<unsigned>(name.size()), GNUTLS_FSAN_APPEND);
        }
        for (const auto &address : server.addresses) {
            gnutls_x509_crt_set_subject_alt_name(
                certificate, GNUTLS_SAN_IPADDRESS, address.data(), address.size(), GNUTLS_FSAN_APPEND);
        }
        sign(certificate, m_certificate, m_key);
        Certificate made{exported(certificate), {}};
        gnutls_datum_t pem{};
        gnutls_x509_privkey_export2(key, GNUTLS_X509_FMT_PEM, &pem);
        made.keyPem.assign(reinterpret_cast<const char *>(pem.data), pem.size);
        gnutls_free(pem.data);
        gnutls_x509_crt_deinit(certificate);
        gnutls_x509_privkey_deinit(key);
        return made;
    }

private:
    static void
    fill(gnutls_x509_crt_t certificate, gnutls_x509_privkey_t key, const std::string &name, long from, long until)
    {
        static std::atomic<unsigned> serial{1};
        const unsigned number = serial++;
        const std::array<unsigned char, 4> bytes{0x01,
                                                 static_cast<unsigned char>(number >> 16),
                                                 static_cast<unsigned char>(number >> 8),
                                                 static_cast<unsigned char>(number)};
        gnutls_x509_crt_set_version(certificate, 3);
        gnutls_x509_crt_set_serial(certificate, bytes.data(), bytes.size());
        const std::time_t now = std::time(nullptr);
        gnutls_x509_crt_set_activation_time(certificate, now + from);
        gnutls_x509_crt_set_expiration_time(certificate, now + until);
        gnutls_x509_crt_set_dn_by_oid(
            certificate, GNUTLS_OID_X520_COMMON_NAME, 0, name.data(), static_cast<unsigned>(name.size()));
        gnutls_x509_crt_set_key(certificate, key);
    }

    /// Signs \a certificate as \a issuer, whose key is \a key.
    static void sign(gnutls_x509_crt_t certificate, gnutls_x509_crt_t issuer, gnutls_x509_privkey_t key)
    {
        gnutls_x509_crt_sign2(certificate, issuer, key, GNUTLS_DIG_SHA256, 0);
    }

    static std::string exported(gnutls_x509_crt_t certificate)
    {
        gnutls_datum_t pem{};
        gnutls_x509_crt_export2(certificate, GNUTLS_X509_FMT_PEM, &pem);
        std::string text(reinterpret_cast<const char *>(pem.data), pem.size);
        gnutls_free(pem.data);
        return text;
    }

    gnutls_x509_privkey_t m_key = nullptr;
    gnutls_x509_crt_t m_certificate = nullptr;
    std::string m_pem;
    std::filesystem::path m_file;
};

/// A listening socket on the loopback, at a port of its own.
inline int listenOnLoopback(std::uint16_t &port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    const int on = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    (void)::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address);
    (void)::listen(fd, 8);
    socklen_t length = sizeof address;
    (void)::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    port = ntohs(address.sin_port);
    return fd;
}

/// **TLS in front of a plain server**: takes TLS on its own port with the
/// certificate it is given, and passes everything through to \a behind on
/// the loopback, both ways.
class TlsFront
{
public:
    /// \a priority is the TLS versions and ciphers the front takes, in
    /// GnuTLS's words; empty for the system's.
    TlsFront(const Certificate &certificate, std::uint16_t behind, std::string priority = {})
        : m_behind(behind)
        , m_priority(std::move(priority))
    {
        gnutls_certificate_allocate_credentials(&m_credentials);
        const gnutls_datum_t pem{
            reinterpret_cast<unsigned char *>(const_cast<char *>(certificate.certificatePem.data())),
            static_cast<unsigned>(certificate.certificatePem.size())};
        const gnutls_datum_t key{reinterpret_cast<unsigned char *>(const_cast<char *>(certificate.keyPem.data())),
                                 static_cast<unsigned>(certificate.keyPem.size())};
        gnutls_certificate_set_x509_key_mem(m_credentials, &pem, &key, GNUTLS_X509_FMT_PEM);
        m_listen = listenOnLoopback(m_port);
        m_thread = std::thread([this] { run(); });
    }

    ~TlsFront()
    {
        m_stopping = true;
        m_thread.join();
        for (std::thread &client : m_clients) {
            client.join();
        }
        ::close(m_listen);
        gnutls_certificate_free_credentials(m_credentials);
    }

    TlsFront(const TlsFront &) = delete;
    TlsFront &operator=(const TlsFront &) = delete;

    [[nodiscard]] std::uint16_t port() const
    {
        return m_port;
    }

    /// How many clients finished a handshake.
    [[nodiscard]] int handshakes() const
    {
        return m_handshakes;
    }

    /// How many clients started one and were refused, or gave up.
    [[nodiscard]] int refusals() const
    {
        return m_refusals;
    }

private:
    void run()
    {
        while (!m_stopping) {
            pollfd entry{m_listen, POLLIN, 0};
            if (::poll(&entry, 1, 20) <= 0) {
                continue;
            }
            const int fd = ::accept4(m_listen, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd >= 0) {
                m_clients.emplace_back([this, fd] { serve(fd); });
            }
        }
    }

    void serve(int fd)
    {
        gnutls_session_t session = nullptr;
        gnutls_init(&session, GNUTLS_SERVER | GNUTLS_NO_SIGNAL);
        if (m_priority.empty()) {
            gnutls_set_default_priority(session);
        } else {
            gnutls_priority_set_direct(session, m_priority.c_str(), nullptr);
        }
        gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, m_credentials);
        gnutls_transport_set_int(session, fd);
        gnutls_handshake_set_timeout(session, 5000);
        int result = 0;
        do {
            result = gnutls_handshake(session);
        } while (result < 0 && gnutls_error_is_fatal(result) == 0 && !m_stopping);
        if (result < 0) {
            ++m_refusals;
            gnutls_deinit(session);
            ::close(fd);
            return;
        }
        ++m_handshakes;

        const int plain = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(m_behind);
        if (::connect(plain, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
            ::close(plain);
            gnutls_deinit(session);
            ::close(fd);
            return;
        }
        std::array<char, 16384> buffer{};
        bool open = true;
        while (open && !m_stopping) {
            std::array<pollfd, 2> entries{pollfd{fd, POLLIN, 0}, pollfd{plain, POLLIN, 0}};
            const bool pending = gnutls_record_check_pending(session) > 0;
            if (!pending && ::poll(entries.data(), entries.size(), 20) <= 0) {
                continue;
            }
            if (pending || (entries[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                const ssize_t got = gnutls_record_recv(session, buffer.data(), buffer.size());
                if (got > 0) {
                    open = ::send(plain, buffer.data(), static_cast<std::size_t>(got), MSG_NOSIGNAL) == got;
                } else if (got != GNUTLS_E_AGAIN && got != GNUTLS_E_INTERRUPTED) {
                    open = false;
                }
            }
            if ((entries[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                const ssize_t got = ::recv(plain, buffer.data(), buffer.size(), 0);
                if (got > 0) {
                    open = gnutls_record_send(session, buffer.data(), static_cast<std::size_t>(got)) == got;
                } else {
                    // The plain server hung up: so does the front, as
                    // Icecast over TLS would.
                    (void)gnutls_bye(session, GNUTLS_SHUT_WR);
                    open = false;
                }
            }
        }
        ::close(plain);
        gnutls_deinit(session);
        ::close(fd);
    }

    std::uint16_t m_behind = 0;
    std::string m_priority;
    gnutls_certificate_credentials_t m_credentials = nullptr;
    int m_listen = -1;
    std::uint16_t m_port = 0;
    std::atomic<bool> m_stopping{false};
    std::atomic<int> m_handshakes{0};
    std::atomic<int> m_refusals{0};
    std::vector<std::thread> m_clients;
    std::thread m_thread;
};

/// **A server that takes the connection and then says nothing**, ever: a
/// TLS handshake to it stalls at its first step.
class SilentServer
{
public:
    SilentServer()
    {
        m_listen = listenOnLoopback(m_port);
        m_thread = std::thread([this] {
            std::vector<int> accepted;
            while (!m_stopping) {
                pollfd entry{m_listen, POLLIN, 0};
                if (::poll(&entry, 1, 20) > 0) {
                    if (const int fd = ::accept4(m_listen, nullptr, nullptr, SOCK_CLOEXEC); fd >= 0) {
                        accepted.push_back(fd);
                    }
                }
            }
            for (const int fd : accepted) {
                ::close(fd);
            }
        });
    }

    ~SilentServer()
    {
        m_stopping = true;
        m_thread.join();
        ::close(m_listen);
    }

    SilentServer(const SilentServer &) = delete;
    SilentServer &operator=(const SilentServer &) = delete;

    [[nodiscard]] std::uint16_t port() const
    {
        return m_port;
    }

private:
    int m_listen = -1;
    std::uint16_t m_port = 0;
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};

} // namespace rb::test
