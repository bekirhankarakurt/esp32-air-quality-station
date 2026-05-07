// aq_simulator: runs the real station pipeline (drivers, CRC checks, recovery,
// filters, AQI, encoders) against emulated SHT31/SCD41 sensors in a simulated
// room and streams the telemetry over UDP, exactly like the firmware does.

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <thread>

#include "aq/config.hpp"
#include "aq/sim/room.hpp"
#include "aq/sim/sim_bus.hpp"
#include "aq/station.hpp"
#include "aq/telemetry.hpp"
#include "cli.hpp"
#include "udp_socket.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*sig*/) { g_stop = 1; }

struct Options {
    aq::Ipv4 ip{127, 0, 0, 1};
    std::uint16_t port{47800};
    aq::TelemetryFormat format{aq::TelemetryFormat::Binary};
    std::uint32_t period_ms{5000};
    double speed{1.0};
    std::uint64_t count{0};
    std::uint64_t seed{1};
    double fault_rate{0.0};
    bool quiet{false};
};

void usage(std::FILE* out) {
    std::fputs(
        "Usage: aq_simulator [options]\n"
        "Emulates an ESP32 air-quality station (SHT31 + SCD41 in a simulated room)\n"
        "and streams telemetry over UDP.\n\n"
        "  --target IP:PORT    destination (default 127.0.0.1:47800)\n"
        "  --format F          binary | json | both (default binary)\n"
        "  --period-ms N       simulated sample period, 1000-3600000 (default 5000)\n"
        "  --speed X           simulated seconds per real second, 1-10000 (default 1)\n"
        "  --count N           stop after N samples (default: run until Ctrl-C)\n"
        "  --seed N            random seed for the room and sensor noise (default 1)\n"
        "  --fault-rate P      probability 0-0.5 of an I2C NACK / bit flip per transfer\n"
        "  --quiet             do not print samples\n"
        "  -h, --help          show this help\n",
        out);
}

bool parse_target(std::string_view s, Options& o) {
    const std::size_t colon = s.rfind(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const auto ip = aq::parse_ipv4(s.substr(0, colon));
    const auto port = aqhost::parse_u64(s.substr(colon + 1), 1, 65535);
    if (!ip || !port) {
        return false;
    }
    o.ip = *ip;
    o.port = static_cast<std::uint16_t>(*port);
    return true;
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(stdout);
            std::exit(0);
        }
        if (a == "--quiet") {
            o.quiet = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %s\n", argv[i]);
            return false;
        }
        const std::string_view v = argv[++i];
        bool ok = true;
        if (a == "--target") {
            ok = parse_target(v, o);
        } else if (a == "--format") {
            const auto f = aq::parse_format(v);
            ok = f.has_value();
            o.format = f.value_or(o.format);
        } else if (a == "--period-ms") {
            const auto n = aqhost::parse_u64(v, aq::limits::kMinPeriodMs, aq::limits::kMaxPeriodMs);
            ok = n.has_value();
            o.period_ms = static_cast<std::uint32_t>(n.value_or(o.period_ms));
        } else if (a == "--speed") {
            const auto x = aqhost::parse_fraction(v, 10000.0);
            ok = x.has_value() && *x >= 1.0;
            o.speed = x.value_or(o.speed);
        } else if (a == "--count") {
            const auto n = aqhost::parse_u64(v, 1, UINT64_MAX / 2);
            ok = n.has_value();
            o.count = n.value_or(0);
        } else if (a == "--seed") {
            const auto n = aqhost::parse_u64(v, 0, UINT64_MAX / 2);
            ok = n.has_value();
            o.seed = n.value_or(o.seed);
        } else if (a == "--fault-rate") {
            const auto p = aqhost::parse_fraction(v, 0.5);
            ok = p.has_value();
            o.fault_rate = p.value_or(0.0);
        } else {
            std::fprintf(stderr, "unknown option %s\n", argv[i - 1]);
            return false;
        }
        if (!ok) {
            std::fprintf(stderr, "invalid value for %s: %s\n", argv[i - 1], argv[i]);
            return false;
        }
    }
    return true;
}

void print_sample(const aq::Reading& r, const aq::sim::RoomModel& room, std::uint32_t faults) {
    std::array<char, 16> co2{"   --"};
    if (r.has(aq::flags::kCo2Valid)) {
        std::snprintf(co2.data(), co2.size(), "%5u", static_cast<unsigned>(r.co2_ppm));
    }
    std::printf("seq=%-5llu t=%6.0fs  %5.2f C  %5.1f %%RH  dew %5.1f C  CO2 %s ppm  AQI %3u %-9s occ=%d %s faults=%u\n",
                static_cast<unsigned long long>(r.seq), room.elapsed_s(), static_cast<double>(r.temperature_c),
                static_cast<double>(r.humidity_pct), static_cast<double>(r.dew_point_c), co2.data(),
                static_cast<unsigned>(r.aqi), aq::to_string(r.level), room.occupants(),
                room.window_open() ? "window=open  " : "window=closed", faults);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        usage(stderr);
        return 2;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    aqhost::UdpSocket sock;
    if (!sock.valid()) {
        std::perror("socket");
        return 1;
    }

    aq::sim::RoomModel room(opt.seed);
    aq::sim::SimI2cBus bus(opt.seed ^ 0x5EED5EEDULL);
    aq::sim::SimSht31 sht(room);
    aq::sim::SimScd41 scd(room);
    if (!bus.attach(sht) || !bus.attach(scd)) {
        return 1;
    }
    bus.set_fault_rate(opt.fault_rate);

    aq::Station station(bus, aq::StationSettings{0x0051A001U, 6, 0, {}});
    if (const auto err = station.begin(); err != aq::SensorError::None) {
        std::fprintf(stderr, "SCD41 start failed: %s (will retry)\n", aq::to_string(err));
    }

    std::array<char, 16> ip{};
    static_cast<void>(aq::format_ipv4(opt.ip, ip.data(), ip.size()));
    std::fprintf(stderr, "aq_simulator: %s -> %s:%u, period %u ms, speed x%g, seed %llu\n", aq::to_string(opt.format),
                 ip.data(), static_cast<unsigned>(opt.port), static_cast<unsigned>(opt.period_ms), opt.speed,
                 static_cast<unsigned long long>(opt.seed));

    const auto real_period = std::chrono::duration<double, std::milli>(opt.period_ms / opt.speed);
    auto next = std::chrono::steady_clock::now();
    std::uint64_t sent = 0;
    std::uint64_t send_errors = 0;
    std::array<char, aq::kJsonBufferSize> json{};

    for (std::uint64_t i = 0; g_stop == 0 && (opt.count == 0 || i < opt.count); ++i) {
        room.step(opt.period_ms / 1000.0);
        bus.advance(opt.period_ms);
        const aq::Reading r = station.sample(bus.now_ms() * 1'000'000ULL);

        if (opt.format != aq::TelemetryFormat::Json) {
            const auto pkt = aq::encode_binary(r);
            sock.send_to(opt.ip, opt.port, pkt.data(), pkt.size()) ? ++sent : ++send_errors;
        }
        if (opt.format != aq::TelemetryFormat::Binary) {
            const std::size_t n = aq::encode_json(r, json.data(), json.size());
            sock.send_to(opt.ip, opt.port, json.data(), n) ? ++sent : ++send_errors;
        }
        if (!opt.quiet) {
            print_sample(r, room, bus.faults_injected());
        }

        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(real_period);
        std::this_thread::sleep_until(next);
    }

    std::fprintf(stderr,
                 "aq_simulator: %llu datagrams sent, %llu send errors, %u injected faults, "
                 "SHT31 errors %u (recoveries %u), SCD41 errors %u (recoveries %u)\n",
                 static_cast<unsigned long long>(sent), static_cast<unsigned long long>(send_errors),
                 bus.faults_injected(), station.sht31_health().errors, station.sht31_health().recoveries,
                 station.scd41_health().errors, station.scd41_health().recoveries);
    return 0;
}
