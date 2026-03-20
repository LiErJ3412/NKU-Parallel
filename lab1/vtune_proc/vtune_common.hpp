#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

namespace vtune_proc {

inline volatile double sink = 0.0;

inline double deterministic_value(std::size_t i, std::size_t j) {
    const std::uint64_t mix = (i + 1) * 1315423911ull + (j + 7) * 2654435761ull;
    return static_cast<double>(mix % 1024ull) / 257.0 - 1.5;
}

inline void generate_matrix_case(std::size_t n, std::vector<double> &matrix,
                                 std::vector<double> &vec) {
    matrix.resize(n * n);
    vec.resize(n);
    for (std::size_t row = 0; row < n; ++row) {
        vec[row] = deterministic_value(row, row + 3);
        for (std::size_t col = 0; col < n; ++col) {
            matrix[row * n + col] = deterministic_value(row, col);
        }
    }
}

inline void generate_sum_case(std::size_t n, std::vector<double> &data) {
    data.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = deterministic_value(i, i / 3 + 11);
    }
}

NOINLINE inline void matrix_dot_naive(const double *matrix, const double *vec,
                                      double *out, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        double sum = 0.0;
        for (std::size_t row = 0; row < n; ++row) {
            sum += matrix[row * n + col] * vec[row];
        }
        out[col] = sum;
    }
}

NOINLINE inline void matrix_dot_row_major(const double *matrix,
                                          const double *vec, double *out,
                                          std::size_t n) {
    std::fill(out, out + n, 0.0);
    for (std::size_t row = 0; row < n; ++row) {
        const double factor = vec[row];
        const double *row_ptr = matrix + row * n;
        for (std::size_t col = 0; col < n; ++col) {
            out[col] += row_ptr[col] * factor;
        }
    }
}

NOINLINE inline double sum_chain(const double *data, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += data[i];
    }
    return sum;
}

NOINLINE inline double sum_two_way(const double *data, std::size_t n) {
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

inline void print_run_banner(const std::string &name, std::size_t n,
                             int repeats) {
    std::cout << "Program: " << name << "\n";
    std::cout << "n = " << n << ", repeats = " << repeats << "\n";
    std::cout << "This executable only contains one target kernel for VTune.\n";
}

} // namespace vtune_proc
