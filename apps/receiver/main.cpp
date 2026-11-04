// aq_receiver: listens for air-quality telemetry (binary TLMY packets and/or
// JSON lines), validates every datagram, prints a live table, optionally logs
// CSV, and reports loss / reordering / rejection statistics on exit.

#include <array>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <memory>
#include <string_view>

#include "aq/telemetry.hpp"
#include "cli.hpp"
#include "udp_socket.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*sig*/) { g_stop = 1; }

struct Options {
    std::uint16_t port{47800};
    const char* csv_path{nullptr};
    std::uint64_t count{0};
    std::uint64_t timeout_s{0};
    bool quiet{false};
};

struct FileCloser {
    void operator()(std::FILE* f) const noexcept { std::fclose(f); }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

/// Loss / ordering tracker for one stream.
struct SeqTracker {
    bool seen{false};
    std::uint64_t last{0};
    std::uint64_t gaps{0};
    std::uint64_t reordered{0};

    void update(std::uint64_t seq) noexcept {
        if (seen && seq > last + 1) {
            gaps += seq - last - 1;
        } else if (seen && seq <= last) {
            ++reordered;
        }
        if (!seen || seq > last) {
            last = seq;
        }
        seen = true;
    }
};

struct Stats {
    std::uint64_t binary{0};
    std::uint64_t json{0};
    std::uint64_t bad_json{0};
    std::array<std::uint64_t, 8> rejected{};
    SeqTracker bin_seq{};
    SeqTracker json_seq{};
};

void usage(std::FILE* out) {
    std::fputs(
        "Usage: aq_receiver [options]\n"
        "Receives air-quality telemetry over UDP (binary and JSON), validates and prints it.\n\n"
        "  --port N         UDP port to listen on (default 47800)\n"
        "  --csv FILE       append every valid sample to FILE as CSV\n"
        "  --count N        exit after N valid samples\n"
        "  --timeout-s N    exit after N seconds without a datagram\n"
        "  --quiet          do not print samples\n"
        "  -h, --help       show this help\n",
        out);
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
        std::optional<std::uint64_t> n;
        if (a == "--port") {
            n = aqhost::parse_u64(v, 1, 65535);
            o.port = static_cast<std::uint16_t>(n.value_or(0));
        } else if (a == "--csv") {
            o.csv_path = argv[i];
            continue;
        } else if (a == "--count") {
            n = aqhost::parse_u64(v, 1, UINT64_MAX / 2);
            o.count = n.value_or(0);
        } else if (a == "--timeout-s") {
            n = aqhost::parse_u64(v, 1, 86400);
            o.timeout_s = n.value_or(0);
        } else {
            std::fprintf(stderr, "unknown option %s\n", argv[i - 1]);
            return false;
        }
        if (!n) {
            std::fprintf(stderr, "invalid value for %s: %s\n", argv[i - 1], argv[i]);
            return false;
        }
    }
    return true;
}

void format_value(std::array<char, 12>& out, bool valid, int decimals, double v) {
    if (valid && std::isfinite(v)) {
        std::snprintf(out.data(), out.size(), "%.*f", decimals, v);
    } else {
        std::snprintf(out.data(), out.size(), "%s", "--");
    }
}

void print_header() {
    std::printf("%-4s %6s %8s %9s %7s %7s %7s %6s %4s %-9s %-6s %s\n", "fmt", "seq", "device", "uptime", "temp_C",
                "rh_%", "dew_C", "co2", "aqi", "level", "flags", "err");
}

void print_row(const aq::Reading& r, const char* fmt) {
    std::array<char, 12> t{};
    std::array<char, 12> rh{};
    std::array<char, 12> dew{};
    std::array<char, 12> co2{};
    const bool thermo = r.has(aq::flags::kThermoValid);
    format_value(t, thermo, 2, static_cast<double>(r.temperature_c));
    format_value(rh, thermo, 1, static_cast<double>(r.humidity_pct));
    format_value(dew, thermo, 1, static_cast<double>(r.dew_point_c));
    format_value(co2, r.has(aq::flags::kCo2Valid), 0, static_cast<double>(r.co2_ppm));
    std::array<char, 8> flags{"----"};
    flags[0] = r.has(aq::flags::kThermoValid) ? 'T' : '-';
    flags[1] = r.has(aq::flags::kCo2Valid) ? 'C' : '-';
    flags[2] = r.has(aq::flags::kThermoFallback) ? 'F' : '-';
    flags[3] = r.has(aq::flags::kComfortable) ? 'K' : '-';
    std::printf("%-4s %6llu %08X %8.0fs %7s %7s %7s %6s %4u %-9s %-6s %u\n", fmt,
                static_cast<unsigned long long>(r.seq), static_cast<unsigned>(r.device_id),
                static_cast<double>(r.timestamp_ns) / 1e9, t.data(), rh.data(), dew.data(), co2.data(),
                static_cast<unsigned>(r.aqi), aq::to_string(r.level), flags.data(),
                static_cast<unsigned>(r.error_count));
    std::fflush(stdout);
}

void log_csv(std::FILE* f, const aq::Reading& r, const char* fmt) {
    std::fprintf(f, "%s,%llu,%llu,%08X,%.3f,%.3f,%.3f,%u,%u,%s,%u,%u\n", fmt, static_cast<unsigned long long>(r.seq),
                 static_cast<unsigned long long>(r.timestamp_ns), static_cast<unsigned>(r.device_id),
                 static_cast<double>(r.temperature_c), static_cast<double>(r.humidity_pct),
                 static_cast<double>(r.dew_point_c), static_cast<unsigned>(r.co2_ppm), static_cast<unsigned>(r.aqi),
                 aq::to_string(r.level), static_cast<unsigned>(r.flags), static_cast<unsigned>(r.error_count));
    std::fflush(f);
}

void print_stats(const Stats& s) {
    std::fprintf(stderr, "aq_receiver: %llu binary, %llu json valid; rejected:",
                 static_cast<unsigned long long>(s.binary), static_cast<unsigned long long>(s.json));
    bool any = false;
    for (std::size_t i = 1; i < s.rejected.size(); ++i) {
        if (s.rejected[i] != 0) {
            std::fprintf(stderr, " %s=%llu", aq::to_string(static_cast<aq::DecodeError>(i)),
                         static_cast<unsigned long long>(s.rejected[i]));
            any = true;
        }
    }
    if (s.bad_json != 0) {
        std::fprintf(stderr, " malformed-json=%llu", static_cast<unsigned long long>(s.bad_json));
        any = true;
    }
    const std::uint64_t lost = s.bin_seq.gaps + s.json_seq.gaps;
    const std::uint64_t reordered = s.bin_seq.reordered + s.json_seq.reordered;
    std::fprintf(stderr, "%s; lost %llu, reordered %llu\n", any ? "" : " none", static_cast<unsigned long long>(lost),
                 static_cast<unsigned long long>(reordered));
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

    const aqhost::UdpSocket sock;
    if (!sock.valid() || !sock.bind_any(opt.port)) {
        std::perror("aq_receiver: bind");
        return 1;
    }

    FilePtr csv;
    if (opt.csv_path != nullptr) {
        csv.reset(std::fopen(opt.csv_path, "a"));
        if (!csv) {
            std::perror(opt.csv_path);
            return 1;
        }
        if (std::ftell(csv.get()) == 0) {
            std::fputs("format,seq,timestamp_ns,device,temp_c,rh_pct,dew_c,co2_ppm,aqi,level,flags,errors\n",
                       csv.get());
        }
    }

    std::fprintf(stderr, "aq_receiver: listening on udp/%u\n", static_cast<unsigned>(opt.port));
    if (!opt.quiet) {
        print_header();
    }

    Stats stats;
    std::array<std::uint8_t, 1500> buf{};
    std::uint64_t idle_ms = 0;
    constexpr int kPollMs = 200;

    while (g_stop == 0 && (opt.count == 0 || stats.binary + stats.json < opt.count)) {
        const auto n = sock.receive(buf.data(), buf.size(), kPollMs);
        if (!n) {
            idle_ms += kPollMs;
            if (opt.timeout_s != 0 && idle_ms >= opt.timeout_s * 1000U) {
                std::fprintf(stderr, "aq_receiver: no data for %llu s, exiting\n",
                             static_cast<unsigned long long>(opt.timeout_s));
                break;
            }
            continue;
        }
        idle_ms = 0;

        aq::Reading r{};
        const char* fmt = "bin";
        if (aq::looks_like_json(buf.data(), *n)) {
            std::string_view line(reinterpret_cast<const char*>(buf.data()), *n);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.remove_suffix(1);
            }
            const auto parsed = aq::parse_json(line);
            if (!parsed) {
                ++stats.bad_json;
                continue;
            }
            r = *parsed;
            fmt = "json";
            ++stats.json;
            stats.json_seq.update(r.seq);
        } else {
            const auto d = aq::decode_binary(buf.data(), *n);
            if (!d.ok()) {
                ++stats.rejected[static_cast<std::size_t>(d.error)];
                continue;
            }
            r = d.reading;
            ++stats.binary;
            stats.bin_seq.update(r.seq);
        }
        if (!opt.quiet) {
            print_row(r, fmt);
        }
        if (csv) {
            log_csv(csv.get(), r, fmt);
        }
    }
    print_stats(stats);
    return 0;
}
