#include "udp_publisher.hpp"

#include "esp_log.h"

namespace fw {
namespace {
constexpr const char* kTag = "udp";
}  // namespace

UdpPublisher::~UdpPublisher() {
    if (sock_ >= 0) {
        close(sock_);
    }
}

bool UdpPublisher::open() noexcept {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ < 0) {
        ESP_LOGE(kTag, "socket() failed: errno %d", errno);
        return false;
    }
    const int one = 1;
    setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    return true;
}

void UdpPublisher::set_target(const aq::Ipv4& ip, std::uint16_t port) noexcept {
    dest_ = sockaddr_in{};
    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(port);
    dest_.sin_addr.s_addr = htonl((static_cast<std::uint32_t>(ip[0]) << 24U) |
                                  (static_cast<std::uint32_t>(ip[1]) << 16U) |
                                  (static_cast<std::uint32_t>(ip[2]) << 8U) | ip[3]);
}

bool UdpPublisher::send(const void* data, std::size_t len) noexcept {
    const auto n = sendto(sock_, data, len, 0, reinterpret_cast<const sockaddr*>(&dest_), sizeof(dest_));
    return n == static_cast<ssize_t>(len);
}

std::uint32_t UdpPublisher::publish(const aq::Reading& r, aq::TelemetryFormat format) noexcept {
    std::uint32_t failed = 0;
    if (format != aq::TelemetryFormat::Json) {
        const aq::Packet pkt = aq::encode_binary(r);
        failed += send(pkt.data(), pkt.size()) ? 0U : 1U;
    }
    if (format != aq::TelemetryFormat::Binary) {
        const std::size_t n = aq::encode_json(r, json_.data(), json_.size());
        failed += (n != 0 && send(json_.data(), n)) ? 0U : 1U;
    }
    return failed;
}

}  // namespace fw
