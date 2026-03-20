#include "vtune_common.hpp"

int main() {
    using namespace vtune_proc;

    constexpr std::size_t n = 1u << 20;
    constexpr int repeats = 10000;

    std::vector<double> data;
    generate_sum_case(n, data);

    print_run_banner("sum_two_way", n, repeats);

    for (int iter = 0; iter < repeats; ++iter) {
        sink += sum_two_way(data.data(), n);
    }

    std::cout << "guard = " << sink << "\n";
    return 0;
}
