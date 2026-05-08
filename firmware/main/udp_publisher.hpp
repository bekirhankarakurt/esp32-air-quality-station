// Sends readings as UDP datagrams (binary TLMY packet and/or JSON line).
#pragma once

#include <array>
#include <cstdint>

#include "aq/config.hpp"
#include "aq/telemetry.hpp"
#include "lwip/sockets.h"

namespace fw {

class UdpPublisher {
public:
    UdpPublisher() = default;
    ~UdpPublisher();
    UdpPublisher(const UdpPublisher&) = delete;
    UdpPublisher& operator=(const UdpPublisher&) = delete;
    UdpPublisher(UdpPublisher&&) = delete;
    UdpPublisher& operator=(UdpPublisher&&) = delete;

    /// Create the socket (broadcast enabled so x.x.x.255 targets work).
    [[nodiscard]] bool open() noexcept;
    void set_target(const aq::Ipv4& ip, std::uint16_t port) noexcept;
    /// Send @p r in @p format; returns the number of datagrams that failed.
    [[nodiscard]] std::uint32_t publish(const aq::Reading& r, aq::TelemetryFormat format) noexcept;

private:
    [[nodiscard]] bool send(const void* data, std::size_t len) noexcept;

    int sock_{-1};
    sockaddr_in dest_{};
    std::array<char, aq::kJsonBufferSize> json_{};
};

}  // namespace fw
