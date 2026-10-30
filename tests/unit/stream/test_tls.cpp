// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A connection made secure, against a TLS server made here with a
// certificate authority of the test's own: a good certificate connects and
// carries the conversation; one for another name, from an authority that
// is not trusted, or out of date is refused, and says which; a server that
// does not speak TLS, or only an old one, is refused; and a handshake that
// stalls is let go of as soon as it is called off.

#include "fakeserver.h"
#include "stream/connection.h"
#include "tlsserver.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace rb::stream;
using namespace rb::test;
using namespace std::chrono_literals;

namespace
{

constexpr std::array<unsigned char, 4> kLoopback{127, 0, 0, 1};

/// The certificate a server on this computer has: its name and its address.
[[nodiscard]] TestAuthority::Server localServer()
{
    TestAuthority::Server server;
    server.names = {"localhost"};
    server.addresses = {kLoopback};
    return server;
}

/// Connects to \a port and makes the connection secure, trusting \a trust.
[[nodiscard]] std::expected<Connection, StreamError> secureConnection(const std::string &host,
                                                                      std::uint16_t port,
                                                                      const std::string &trust,
                                                                      std::chrono::milliseconds timeout = 5s)
{
    const std::atomic<bool> never{false};
    std::expected<Connection, StreamError> connection = Connection::open(host, port, 5s, never);
    if (!connection) {
        return connection;
    }
    if (const std::expected<void, StreamError> secured = connection->startTls(host, TlsOptions{trust}, timeout, never);
        !secured) {
        return std::unexpected(secured.error());
    }
    return connection;
}

/// One plain server that answers whatever comes with an HTTP error, as a
/// web server on a port the stream thought was TLS does.
class PlainHttpServer
{
public:
    PlainHttpServer()
    {
        m_listen = listenOnLoopback(m_port);
        m_thread = std::thread([this] {
            pollfd entry{m_listen, POLLIN, 0};
            if (::poll(&entry, 1, 5000) <= 0) {
                return;
            }
            const int fd = ::accept4(m_listen, nullptr, nullptr, SOCK_CLOEXEC);
            std::array<char, 4096> buffer{};
            pollfd client{fd, POLLIN, 0};
            if (::poll(&client, 1, 5000) > 0) {
                (void)::recv(fd, buffer.data(), buffer.size(), 0);
            }
            const std::string answer = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
            (void)::send(fd, answer.data(), answer.size(), MSG_NOSIGNAL);
            std::this_thread::sleep_for(200ms);
            ::close(fd);
        });
    }

    ~PlainHttpServer()
    {
        m_thread.join();
        ::close(m_listen);
    }

    PlainHttpServer(const PlainHttpServer &) = delete;
    PlainHttpServer &operator=(const PlainHttpServer &) = delete;

    [[nodiscard]] std::uint16_t port() const
    {
        return m_port;
    }

private:
    int m_listen = -1;
    std::uint16_t m_port = 0;
    std::thread m_thread;
};

} // namespace

TEST(TlsTest, AGoodCertificateConnectsAndCarriesTheConversation)
{
    TestAuthority authority;
    FakeStreamServer behind;
    TlsFront front(authority.issue(localServer()), behind.port());

    std::expected<Connection, StreamError> connection =
        secureConnection("localhost", front.port(), authority.trustFile());
    ASSERT_TRUE(connection.has_value()) << streamErrorName(connection.error());
    EXPECT_TRUE(connection->isSecure());

    const std::atomic<bool> never{false};
    const std::string request = "GET /admin/metadata?mode=updinfo&song=x HTTP/1.0\r\nHost: localhost\r\n\r\n";
    ASSERT_TRUE(connection->send(request, 2s, never).has_value());
    const std::expected<std::string, StreamError> answer = connection->receive(
        [](const std::string &received, bool closed) { return parseHttpResponse(received, closed).has_value(); },
        5s,
        never);
    ASSERT_TRUE(answer.has_value()) << streamErrorName(answer.error());
    EXPECT_TRUE(answer->starts_with("HTTP/1.0 200 OK"));
    ASSERT_EQ(behind.titleRequests().size(), 1U);
    EXPECT_EQ(behind.titleRequests()[0], request);
    EXPECT_EQ(front.handshakes(), 1);
}

TEST(TlsTest, AnAddressIsCheckedAgainstTheAddressesTheCertificateNames)
{
    TestAuthority authority;
    FakeStreamServer behind;
    TlsFront byAddress(authority.issue(localServer()), behind.port());
    EXPECT_TRUE(secureConnection("127.0.0.1", byAddress.port(), authority.trustFile()).has_value());

    TestAuthority::Server nameOnly;
    nameOnly.names = {"localhost"};
    TlsFront byName(authority.issue(nameOnly), behind.port());
    const std::expected<Connection, StreamError> refused =
        secureConnection("127.0.0.1", byName.port(), authority.trustFile());
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), StreamError::CertificateWrongHost);
}

TEST(TlsTest, ACertificateForAnotherNameIsRefused)
{
    TestAuthority authority;
    FakeStreamServer behind;
    TestAuthority::Server elsewhere;
    elsewhere.names = {"radio.example.org"};
    TlsFront front(authority.issue(elsewhere), behind.port());

    const std::expected<Connection, StreamError> refused =
        secureConnection("localhost", front.port(), authority.trustFile());
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), StreamError::CertificateWrongHost);
    EXPECT_FALSE(isWorthRetrying(refused.error()));
    EXPECT_TRUE(behind.titleRequests().empty());
}

TEST(TlsTest, ACertificateFromAnAuthorityNotTrustedIsRefused)
{
    TestAuthority trusted;
    TestAuthority stranger("Somebody Else");
    FakeStreamServer behind;
    TlsFront front(stranger.issue(localServer()), behind.port());

    const std::expected<Connection, StreamError> refused =
        secureConnection("localhost", front.port(), trusted.trustFile());
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), StreamError::CertificateUntrusted);
    // **The system's own authorities** do not know the test's either.
    const std::expected<Connection, StreamError> bySystem = secureConnection("localhost", front.port(), {});
    ASSERT_FALSE(bySystem.has_value());
    EXPECT_EQ(bySystem.error(), StreamError::CertificateUntrusted);
}

TEST(TlsTest, AnExpiredCertificateIsRefused)
{
    TestAuthority authority;
    FakeStreamServer behind;
    TestAuthority::Server old = localServer();
    old.from = -86400 * 30;
    old.until = -86400;
    TlsFront front(authority.issue(old), behind.port());

    const std::expected<Connection, StreamError> refused =
        secureConnection("localhost", front.port(), authority.trustFile());
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), StreamError::CertificateExpired);
}

TEST(TlsTest, AServerThatDoesNotSpeakTlsIsRefused)
{
    const PlainHttpServer server;
    const std::expected<Connection, StreamError> refused = secureConnection("localhost", server.port(), {});
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), StreamError::TlsFailed);
    EXPECT_FALSE(isWorthRetrying(refused.error()));
}

TEST(TlsTest, NothingOlderThanTls12IsSpoken)
{
    TestAuthority authority;
    FakeStreamServer behind;
    TlsFront front(authority.issue(localServer()), behind.port(), "NORMAL:-VERS-ALL:+VERS-TLS1.1:+VERS-TLS1.0");

    const std::expected<Connection, StreamError> refused =
        secureConnection("localhost", front.port(), authority.trustFile());
    ASSERT_FALSE(refused.has_value());
    // **How the server says no is its own business**: one GnuTLS sends an
    // alert (TlsFailed), another hangs up without a word (Closed). Either
    // way nothing was agreed on, and nothing was sent.
    EXPECT_TRUE(refused.error() == StreamError::TlsFailed || refused.error() == StreamError::Closed)
        << streamErrorName(refused.error());
    EXPECT_EQ(front.handshakes(), 0);
    EXPECT_TRUE(behind.requests().empty());
}

TEST(TlsTest, AStalledHandshakeIsLetGoOfWhenCalledOff)
{
    const SilentServer server;
    const std::atomic<bool> never{false};
    std::expected<Connection, StreamError> connection = Connection::open("127.0.0.1", server.port(), 2s, never);
    ASSERT_TRUE(connection.has_value());

    std::atomic<bool> cancel{false};
    std::thread caller([&cancel] {
        std::this_thread::sleep_for(200ms);
        cancel = true;
    });
    const auto before = std::chrono::steady_clock::now();
    const std::expected<void, StreamError> secured = connection->startTls("127.0.0.1", {}, 30s, cancel);
    const auto took = std::chrono::steady_clock::now() - before;
    caller.join();
    ASSERT_FALSE(secured.has_value());
    EXPECT_EQ(secured.error(), StreamError::Closed);
    EXPECT_LT(took, 200ms + 100ms);
    EXPECT_FALSE(connection->isSecure());
}

TEST(TlsTest, AStalledHandshakeGivesUpWhenItsTimeIsUp)
{
    const SilentServer server;
    const std::atomic<bool> never{false};
    std::expected<Connection, StreamError> connection = Connection::open("127.0.0.1", server.port(), 2s, never);
    ASSERT_TRUE(connection.has_value());
    const auto before = std::chrono::steady_clock::now();
    const std::expected<void, StreamError> secured = connection->startTls("127.0.0.1", {}, 300ms, never);
    ASSERT_FALSE(secured.has_value());
    EXPECT_EQ(secured.error(), StreamError::TimedOut);
    EXPECT_LT(std::chrono::steady_clock::now() - before, 300ms + 100ms);
}

TEST(TlsTest, ASecureConnectionSeesTheServerHangUp)
{
    TestAuthority authority;
    FakeStreamServer behind;
    TlsFront front(authority.issue(localServer()), behind.port());
    std::expected<Connection, StreamError> connection =
        secureConnection("localhost", front.port(), authority.trustFile());
    ASSERT_TRUE(connection.has_value());
    const std::atomic<bool> never{false};
    ASSERT_TRUE(connection->send(std::string_view{"PUT /live HTTP/1.1\r\n\r\n"}, 2s, never).has_value());
    ASSERT_TRUE(
        connection
            ->receive([](const std::string &received, bool) { return received.find("\r\n\r\n") != std::string::npos; },
                      2s,
                      never)
            .has_value());
    // A TLS 1.3 server's session tickets are read and are not a hang-up.
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(connection->peerClosed());

    behind.hangUp();
    ASSERT_TRUE(connection->send(std::string_view{"audio"}, 2s, never).has_value());
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!connection->peerClosed() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(connection->peerClosed());
}
