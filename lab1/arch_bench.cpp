#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#endif

namespace fs = std::filesystem;

namespace {

volatile double g_sink = 0.0;

double now_seconds() {
#ifdef _WIN32
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) /
           static_cast<double>(freq.QuadPart);
#else
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) +
           static_cast<double>(ts.tv_nsec) * 1e-9;
#endif
}

std::string format_bytes(std::uint64_t bytes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MiB";
    return oss.str();
}

struct CacheDescriptor {
    int level = 0;
    std::string type;
    std::uint32_t line_size = 0;
    std::uint32_t size = 0;
    std::uint32_t instances = 0;
};

struct PlatformInfo {
    std::string cpu_name = "unknown";
    unsigned int logical_threads = std::thread::hardware_concurrency();
    std::uint64_t memory_bytes = 0;
    std::vector<CacheDescriptor> caches;
};

std::string detect_cpu_name() {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    unsigned int max_leaf = __get_cpuid_max(0x80000000u, nullptr);
    if (max_leaf >= 0x80000004u) {
        std::array<unsigned int, 12> brand{};
        __get_cpuid(0x80000002u, &brand[0], &brand[1], &brand[2], &brand[3]);
        __get_cpuid(0x80000003u, &brand[4], &brand[5], &brand[6], &brand[7]);
        __get_cpuid(0x80000004u, &brand[8], &brand[9], &brand[10], &brand[11]);
        std::string name(reinterpret_cast<const char *>(brand.data()),
                         brand.size() * sizeof(unsigned int));
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        auto first = name.find_first_not_of(' ');
        auto last = name.find_last_not_of(' ');
        if (first != std::string::npos && last != std::string::npos) {
            return name.substr(first, last - first + 1);
        }
    }
#endif
    return "unknown";
}

std::vector<CacheDescriptor> detect_caches() {
    std::vector<CacheDescriptor> caches;
#ifdef _WIN32
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &length);
    if (length == 0) {
        return caches;
    }

    std::vector<std::uint8_t> buffer(length);
    if (!GetLogicalProcessorInformationEx(
            RelationCache,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                buffer.data()),
            &length)) {
        return caches;
    }

    std::map<std::tuple<int, DWORD, DWORD, DWORD>, CacheDescriptor> unique;
    std::size_t offset = 0;
    while (offset < length) {
        auto *info =
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                buffer.data() + offset);
        if (info->Relationship == RelationCache) {
            const CACHE_RELATIONSHIP &cache = info->Cache;
            std::string type = "Unknown";
            switch (cache.Type) {
            case CacheData:
                type = "Data";
                break;
            case CacheInstruction:
                type = "Instruction";
                break;
            case CacheUnified:
                type = "Unified";
                break;
            default:
                break;
            }
            auto key = std::make_tuple(static_cast<int>(cache.Level), cache.Type,
                                       cache.CacheSize, cache.LineSize);
            auto &entry = unique[key];
            entry.level = static_cast<int>(cache.Level);
            entry.type = type;
            entry.line_size = cache.LineSize;
            entry.size = cache.CacheSize;
            entry.instances += 1;
        }
        offset += info->Size;
    }

    for (const auto &item : unique) {
        caches.push_back(item.second);
    }
    std::sort(caches.begin(), caches.end(),
              [](const CacheDescriptor &lhs, const CacheDescriptor &rhs) {
                  if (lhs.level != rhs.level) {
                      return lhs.level < rhs.level;
                  }
                  return lhs.type < rhs.type;
              });
#endif
    return caches;
}

std::uint64_t detect_memory_bytes() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullTotalPhys;
    }
#endif
    return 0;
}

PlatformInfo collect_platform_info() {
    PlatformInfo info;
    info.cpu_name = detect_cpu_name();
    info.memory_bytes = detect_memory_bytes();
    info.caches = detect_caches();
    return info;
}

void write_platform_info(const PlatformInfo &info, const fs::path &path) {
    std::ofstream out(path);
    out << "cpu_name," << info.cpu_name << "\n";
    out << "logical_threads," << info.logical_threads << "\n";
    out << "total_memory_bytes," << info.memory_bytes << "\n";
    out << "total_memory_human," << format_bytes(info.memory_bytes) << "\n";
    for (const auto &cache : info.caches) {
        out << "cache,L" << cache.level << "," << cache.type << ","
            << cache.size << "," << cache.line_size << "," << cache.instances
            << "\n";
    }
}

void flush_cache(std::vector<double> &trash) {
    for (double &value : trash) {
        value += 1.0;
    }
    g_sink += trash.front();
}

double deterministic_value(std::size_t i, std::size_t j) {
    const std::uint64_t mix = (i + 1) * 1315423911ull + (j + 7) * 2654435761ull;
    return static_cast<double>(mix % 1024ull) / 257.0 - 1.5;
}

void generate_matrix_case(std::size_t n, std::vector<double> &matrix,
                          std::vector<double> &vec) {
    matrix.resize(n * n);
    vec.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        vec[j] = deterministic_value(j, j + 3);
        for (std::size_t i = 0; i < n; ++i) {
            matrix[j * n + i] = deterministic_value(j, i);
        }
    }
}

void generate_sum_case(std::size_t n, std::vector<double> &data) {
    data.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = deterministic_value(i, i / 3 + 11);
    }
}

void matrix_dot_naive(const double *matrix, const double *vec, double *out,
                      std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            sum += matrix[j * n + i] * vec[j];
        }
        out[i] = sum;
    }
}

void matrix_dot_row_major(const double *matrix, const double *vec, double *out,
                          std::size_t n) {
    std::fill(out, out + n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        const double factor = vec[j];
        const double *row = matrix + j * n;
        for (std::size_t i = 0; i < n; ++i) {
            out[i] += row[i] * factor;
        }
    }
}

void matrix_dot_row_major_unroll4(const double *matrix, const double *vec,
                                  double *out, std::size_t n) {
    std::fill(out, out + n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        const double factor = vec[j];
        const double *row = matrix + j * n;
        std::size_t i = 0;
        for (; i + 3 < n; i += 4) {
            out[i] += row[i] * factor;
            out[i + 1] += row[i + 1] * factor;
            out[i + 2] += row[i + 2] * factor;
            out[i + 3] += row[i + 3] * factor;
        }
        for (; i < n; ++i) {
            out[i] += row[i] * factor;
        }
    }
}

double sum_chain(const double *data, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += data[i];
    }
    return sum;
}

double sum_two_way(const double *data, std::size_t n) {
    double s0 = 0.0;
    double s1 = 0.0;
    std::size_t i = 0;
    for (; i + 1 < n; i += 2) {
        s0 += data[i];
        s1 += data[i + 1];
    }
    for (; i < n; ++i) {
        s0 += data[i];
    }
    return s0 + s1;
}

double sum_four_way_unroll(const double *data, std::size_t n) {
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    double s3 = 0.0;
    std::size_t i = 0;
    for (; i + 3 < n; i += 4) {
        s0 += data[i];
        s1 += data[i + 1];
        s2 += data[i + 2];
        s3 += data[i + 3];
    }
    double tail = 0.0;
    for (; i < n; ++i) {
        tail += data[i];
    }
    return (s0 + s1) + (s2 + s3) + tail;
}

double sum_pairwise_iterative(const double *data, double *scratch,
                              std::size_t n) {
    std::memcpy(scratch, data, n * sizeof(double));
    for (std::size_t m = n; m > 1; m >>= 1) {
        const std::size_t half = m >> 1;
        std::size_t i = 0;
        for (; i + 3 < half; i += 4) {
            scratch[i] = scratch[2 * i] + scratch[2 * i + 1];
            scratch[i + 1] = scratch[2 * i + 2] + scratch[2 * i + 3];
            scratch[i + 2] = scratch[2 * i + 4] + scratch[2 * i + 5];
            scratch[i + 3] = scratch[2 * i + 6] + scratch[2 * i + 7];
        }
        for (; i < half; ++i) {
            scratch[i] = scratch[2 * i] + scratch[2 * i + 1];
        }
    }
    return scratch[0];
}

template <typename Callable>
double benchmark_callable(Callable &&callable, std::vector<double> &trash,
                          double target_seconds = 0.15, int rounds = 5) {
    int repeats = 1;
    for (;;) {
        flush_cache(trash);
        const double start = now_seconds();
        for (int r = 0; r < repeats; ++r) {
            callable();
        }
        const double elapsed = now_seconds() - start;
        if (elapsed >= target_seconds || repeats >= (1 << 20)) {
            break;
        }
        repeats *= 2;
    }

    double best = std::numeric_limits<double>::max();
    for (int round = 0; round < rounds; ++round) {
        flush_cache(trash);
        const double start = now_seconds();
        for (int r = 0; r < repeats; ++r) {
            callable();
        }
        const double elapsed = now_seconds() - start;
        best = std::min(best, elapsed / static_cast<double>(repeats));
    }
    return best * 1000.0;
}

struct MatrixBenchRow {
    std::size_t n = 0;
    double working_set_mib = 0.0;
    double naive_ms = 0.0;
    double optimized_ms = 0.0;
    double unroll_ms = 0.0;
    double speedup_cache = 0.0;
    double speedup_unroll = 0.0;
    double max_diff_cache = 0.0;
    double max_diff_unroll = 0.0;
};

struct SumBenchRow {
    std::size_t n = 0;
    double data_mib = 0.0;
    double chain_ms = 0.0;
    double two_way_ms = 0.0;
    double four_way_ms = 0.0;
    double pairwise_ms = 0.0;
    double speedup_two_way = 0.0;
    double speedup_four_way = 0.0;
    double speedup_pairwise = 0.0;
    double abs_diff_two_way = 0.0;
    double abs_diff_four_way = 0.0;
    double abs_diff_pairwise = 0.0;
};

double max_abs_diff(const std::vector<double> &lhs,
                    const std::vector<double> &rhs) {
    double diff = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff = std::max(diff, std::abs(lhs[i] - rhs[i]));
    }
    return diff;
}

std::vector<MatrixBenchRow>
run_matrix_benchmarks(const std::vector<std::size_t> &sizes,
                      std::vector<double> &trash) {
    std::vector<MatrixBenchRow> rows;
    rows.reserve(sizes.size());

    for (std::size_t n : sizes) {
        std::vector<double> matrix;
        std::vector<double> vec;
        std::vector<double> out_naive(n);
        std::vector<double> out_opt(n);
        std::vector<double> out_unroll(n);
        generate_matrix_case(n, matrix, vec);

        matrix_dot_naive(matrix.data(), vec.data(), out_naive.data(), n);
        matrix_dot_row_major(matrix.data(), vec.data(), out_opt.data(), n);
        matrix_dot_row_major_unroll4(matrix.data(), vec.data(), out_unroll.data(),
                                     n);

        const double naive_ms = benchmark_callable(
            [&] {
                matrix_dot_naive(matrix.data(), vec.data(), out_naive.data(), n);
                g_sink += out_naive[n / 2];
            },
            trash);

        const double opt_ms = benchmark_callable(
            [&] {
                matrix_dot_row_major(matrix.data(), vec.data(), out_opt.data(), n);
                g_sink += out_opt[n / 2];
            },
            trash);

        const double unroll_ms = benchmark_callable(
            [&] {
                matrix_dot_row_major_unroll4(matrix.data(), vec.data(),
                                             out_unroll.data(), n);
                g_sink += out_unroll[n / 2];
            },
            trash);

        MatrixBenchRow row;
        row.n = n;
        row.working_set_mib =
            static_cast<double>(matrix.size() + vec.size() + out_naive.size()) *
            sizeof(double) / (1024.0 * 1024.0);
        row.naive_ms = naive_ms;
        row.optimized_ms = opt_ms;
        row.unroll_ms = unroll_ms;
        row.speedup_cache = naive_ms / opt_ms;
        row.speedup_unroll = naive_ms / unroll_ms;
        row.max_diff_cache = max_abs_diff(out_naive, out_opt);
        row.max_diff_unroll = max_abs_diff(out_naive, out_unroll);
        rows.push_back(row);
    }

    return rows;
}

std::vector<SumBenchRow> run_sum_benchmarks(const std::vector<std::size_t> &sizes,
                                            std::vector<double> &trash) {
    std::vector<SumBenchRow> rows;
    rows.reserve(sizes.size());

    for (std::size_t n : sizes) {
        std::vector<double> data;
        std::vector<double> scratch(n);
        generate_sum_case(n, data);

        const double ref = sum_chain(data.data(), n);
        const double two = sum_two_way(data.data(), n);
        const double four = sum_four_way_unroll(data.data(), n);
        const double pairwise =
            sum_pairwise_iterative(data.data(), scratch.data(), n);

        const double chain_ms = benchmark_callable(
            [&] {
                g_sink += sum_chain(data.data(), n);
            },
            trash);

        const double two_way_ms = benchmark_callable(
            [&] {
                g_sink += sum_two_way(data.data(), n);
            },
            trash);

        const double four_way_ms = benchmark_callable(
            [&] {
                g_sink += sum_four_way_unroll(data.data(), n);
            },
            trash);

        const double pairwise_ms = benchmark_callable(
            [&] {
                g_sink += sum_pairwise_iterative(data.data(), scratch.data(), n);
            },
            trash);

        SumBenchRow row;
        row.n = n;
        row.data_mib = static_cast<double>(n * sizeof(double)) /
                       (1024.0 * 1024.0);
        row.chain_ms = chain_ms;
        row.two_way_ms = two_way_ms;
        row.four_way_ms = four_way_ms;
        row.pairwise_ms = pairwise_ms;
        row.speedup_two_way = chain_ms / two_way_ms;
        row.speedup_four_way = chain_ms / four_way_ms;
        row.speedup_pairwise = chain_ms / pairwise_ms;
        row.abs_diff_two_way = std::abs(ref - two);
        row.abs_diff_four_way = std::abs(ref - four);
        row.abs_diff_pairwise = std::abs(ref - pairwise);
        rows.push_back(row);
    }

    return rows;
}

void write_matrix_csv(const std::vector<MatrixBenchRow> &rows,
                      const fs::path &path) {
    std::ofstream out(path);
    out << "n,working_set_mib,naive_ms,optimized_ms,unroll_ms,"
           "speedup_cache,speedup_unroll,max_diff_cache,max_diff_unroll\n";
    out << std::fixed << std::setprecision(6);
    for (const auto &row : rows) {
        out << row.n << ',' << row.working_set_mib << ',' << row.naive_ms << ','
            << row.optimized_ms << ',' << row.unroll_ms << ','
            << row.speedup_cache << ',' << row.speedup_unroll << ','
            << row.max_diff_cache << ',' << row.max_diff_unroll << '\n';
    }
}

void write_sum_csv(const std::vector<SumBenchRow> &rows, const fs::path &path) {
    std::ofstream out(path);
    out << "n,data_mib,chain_ms,two_way_ms,four_way_ms,pairwise_ms,"
           "speedup_two_way,speedup_four_way,speedup_pairwise,"
           "abs_diff_two_way,abs_diff_four_way,abs_diff_pairwise\n";
    out << std::fixed << std::setprecision(6);
    for (const auto &row : rows) {
        out << row.n << ',' << row.data_mib << ',' << row.chain_ms << ','
            << row.two_way_ms << ',' << row.four_way_ms << ','
            << row.pairwise_ms << ',' << row.speedup_two_way << ','
            << row.speedup_four_way << ',' << row.speedup_pairwise << ','
            << row.abs_diff_two_way << ',' << row.abs_diff_four_way << ','
            << row.abs_diff_pairwise << '\n';
    }
}

template <typename RowPrinter>
void print_table_header(const std::string &title) {
    std::cout << "\n=== " << title << " ===\n";
}

void print_matrix_summary(const std::vector<MatrixBenchRow> &rows) {
    print_table_header<MatrixBenchRow>("Matrix dot product");
    std::cout << "n\tset(MiB)\tnaive(ms)\tcache(ms)\tunroll(ms)\tspd(cache)\tspd(unroll)\n";
    for (const auto &row : rows) {
        std::cout << row.n << '\t' << std::fixed << std::setprecision(2)
                  << row.working_set_mib << '\t' << std::setprecision(4)
                  << row.naive_ms << '\t' << row.optimized_ms << '\t'
                  << row.unroll_ms << '\t' << std::setprecision(2)
                  << row.speedup_cache << '\t' << row.speedup_unroll << '\n';
    }
}

void print_sum_summary(const std::vector<SumBenchRow> &rows) {
    print_table_header<SumBenchRow>("Array reduction");
    std::cout << "n\tdata(MiB)\tchain(ms)\t2way(ms)\t4way(ms)\tpair(ms)\tspd(2way)\tspd(4way)\tspd(pair)\n";
    for (const auto &row : rows) {
        std::cout << row.n << '\t' << std::fixed << std::setprecision(2)
                  << row.data_mib << '\t' << std::setprecision(4)
                  << row.chain_ms << '\t' << row.two_way_ms << '\t'
                  << row.four_way_ms << '\t' << row.pairwise_ms << '\t'
                  << std::setprecision(2) << row.speedup_two_way << '\t'
                  << row.speedup_four_way << '\t' << row.speedup_pairwise
                  << '\n';
    }
}

bool has_flag(const std::vector<std::string> &args, const std::string &flag) {
    return std::find(args.begin(), args.end(), flag) != args.end();
}

std::string get_option(const std::vector<std::string> &args,
                       const std::string &flag,
                       const std::string &fallback) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) {
            return args[i + 1];
        }
    }
    return fallback;
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    const bool study_mode = has_flag(args, "--study");
    const std::string tag = get_option(args, "--tag", "default");

    const std::vector<std::size_t> matrix_sizes =
        study_mode ? std::vector<std::size_t>{1024}
                   : std::vector<std::size_t>{64, 128, 192, 256, 384, 512,
                                              768, 1024, 1536, 2048};
    const std::vector<std::size_t> sum_sizes =
        study_mode ? std::vector<std::size_t>{1u << 24}
                   : std::vector<std::size_t>{1u << 10, 1u << 12, 1u << 14,
                                              1u << 16, 1u << 18, 1u << 20,
                                              1u << 22, 1u << 24};

    fs::create_directories("results");

    const PlatformInfo platform = collect_platform_info();
    write_platform_info(platform,
                        fs::path("results") / ("platform_" + tag + ".txt"));

    std::vector<double> cache_trash((64u * 1024u * 1024u) / sizeof(double), 1.0);

    std::cout << "CPU: " << platform.cpu_name << '\n';
    std::cout << "Logical threads: " << platform.logical_threads << '\n';
    std::cout << "Memory: " << format_bytes(platform.memory_bytes) << '\n';
    if (!platform.caches.empty()) {
        std::cout << "Caches:\n";
        for (const auto &cache : platform.caches) {
            std::cout << "  L" << cache.level << ' ' << cache.type << ' '
                      << format_bytes(cache.size) << ", line " << cache.line_size
                      << " B, instances " << cache.instances << '\n';
        }
    }

    const auto matrix_rows = run_matrix_benchmarks(matrix_sizes, cache_trash);
    const auto sum_rows = run_sum_benchmarks(sum_sizes, cache_trash);

    write_matrix_csv(matrix_rows,
                     fs::path("results") / ("matrix_" + tag + ".csv"));
    write_sum_csv(sum_rows, fs::path("results") / ("sum_" + tag + ".csv"));

    print_matrix_summary(matrix_rows);
    print_sum_summary(sum_rows);

    std::cout << "\nSink guard: " << std::setprecision(6) << g_sink << '\n';
    return 0;
}
