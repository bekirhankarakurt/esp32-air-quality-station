// RAII wrapper around a POSIX IPv4 UDP socket (host tools only).
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "aq/config.hpp"

namespace aqhost {

class UdpSocket {
public:
    UdpSocket() noexcept : fd_(::socket(AF_INET, SOCK_DGRAM, 0)) {}
    ~UdpSocket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    UdpSocket& operator=(UdpSocket&& o) noexcept {
        if (this != &o) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = std::exchange(o.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    /// Bind to 0.0.0.0:@p port (SO_REUSEADDR set).
    [[nodiscard]] bool bind_any(std::uint16_t port) const noexcept {
        const int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        return ::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    [[nodiscard]] bool send_to(const aq::Ipv4& ip, std::uint16_t port, const void* data, std::size_t len) const noexcept {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl((static_cast<std::uint32_t>(ip[0]) << 24U) |
                                     (static_cast<std::uint32_t>(ip[1]) << 16U) |
                                     (static_cast<std::uint32_t>(ip[2]) << 8U) | ip[3]);
        const auto n = ::sendto(fd_, data, len, 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        return n == static_cast<ssize_t>(len);
    }

    /// Wait up to @p timeout_ms for a datagram. Returns its size, or std::nullopt on timeout/EINTR.
    [[nodiscard]] std::optional<std::size_t> receive(void* buf, std::size_t cap, int timeout_ms) const noexcept {
        pollfd pfd{fd_, POLLIN, 0};
        if (::poll(&pfd, 1, timeout_ms) <= 0) {
            return std::nullopt;
        }
        const auto n = ::recv(fd_, buf, cap, 0);
        if (n < 0) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(n);
    }

private:
    int fd_;
};

}  // namespace aqhost
