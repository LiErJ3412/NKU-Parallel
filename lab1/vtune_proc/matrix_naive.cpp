#include "vtune_common.hpp"

int main() {
    using namespace vtune_proc;

    constexpr std::size_t n = 2048;
    constexpr int repeats = 80;

    std::vector<double> matrix;
    std::vector<double> vec;
    std::vector<double> out(n);
    generate_matrix_case(n, matrix, vec);

    print_run_banner("matrix_dot_naive", n, repeats);

    for (int iter = 0; iter < repeats; ++iter) {
        matrix_dot_naive(matrix.data(), vec.data(), out.data(), n);
        sink += out[(iter * 17) % n];
    }

    std::cout << "guard = " << sink << "\n";
    return 0;
}
