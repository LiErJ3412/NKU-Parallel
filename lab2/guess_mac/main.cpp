#include "PCFG.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include "md5.h"
#include <iomanip>
using namespace std;
using namespace chrono;

// 编译指令如下
// g++ -std=c++17 main.cpp train.cpp guessing.cpp md5.cpp -o main
// g++ -std=c++17 main.cpp train.cpp guessing.cpp md5.cpp -o main -O1
// g++ -std=c++17 main.cpp train.cpp guessing.cpp md5.cpp -o main -O2

int main()
{
    //下面代码用于测试MD5哈希的正确性
    cout << "Testing MD5Hash correctness..." << endl;
    string test_pws[8] = {"123456", "password", "12345678", "qwerty", "123456789", "12345", "1234", "111111"};
    string test_hashes[8] = {
        "e10adc3949ba59abbe56e057f20f883e",
        "5f4dcc3b5aa765d61d8327deb882cf99",
        "25d55ad283aa400af464c76d713c07ad",
        "d8578edf8458ce06fbc5bb76a58c5ca4",
        "25f9e794323b453885f5181f1b624d0b",
        "827ccb0eea8a706c4c34a16891f84e7b",
        "81dc9bdb52d04dc20036dbd8313ed055",
        "96e79218965eb72c92a549dd5a330112"
    };
    for (int i = 0; i < 8; i++) {
        bit32 state[4];
        MD5Hash(test_pws[i], state);
        stringstream ss;
        for (int i1 = 0; i1 < 4; i1 += 1) {
            ss << std::setw(8) << std::setfill('0') << hex << state[i1];
        }
        if (ss.str() != test_hashes[i]) {
            cout << "MD5Hash test failed for " << test_pws[i] << "!" << endl;
            cout << "Expected: " << test_hashes[i] << "\nGot:      " << ss.str() << endl;
            return 1;
        }
    }
    cout << "MD5Hash test passed!" << endl; //请不要修改这一行

    double time_hash = 0;  // 用于MD5哈希的时间
    double time_guess = 0; // 哈希和猜测的总时长
    double time_train = 0; // 模型训练的总时长
#ifdef ENABLE_MANUAL_PROFILE
    double profile_train = 0;
    double profile_order = 0;
    double profile_init = 0;
    double profile_popnext = 0;
    int profile_popnext_calls = 0;
    int profile_hash_batches = 0;
    size_t profile_hashed_passwords = 0;
#endif
    PriorityQueue q;
    auto start_train = system_clock::now();
#ifdef ENABLE_MANUAL_PROFILE
    auto profile_start_train = system_clock::now();
#endif
    q.m.train("../../guess_data/rockyou.txt");
#ifdef ENABLE_MANUAL_PROFILE
    auto profile_end_train = system_clock::now();
    profile_train = double(duration_cast<microseconds>(profile_end_train - profile_start_train).count()) * microseconds::period::num / microseconds::period::den;
    auto profile_start_order = system_clock::now();
#endif
    q.m.order();
#ifdef ENABLE_MANUAL_PROFILE
    auto profile_end_order = system_clock::now();
    profile_order = double(duration_cast<microseconds>(profile_end_order - profile_start_order).count()) * microseconds::period::num / microseconds::period::den;
#endif
    auto end_train = system_clock::now();
    auto duration_train = duration_cast<microseconds>(end_train - start_train);
    time_train = double(duration_train.count()) * microseconds::period::num / microseconds::period::den;

#ifdef ENABLE_MANUAL_PROFILE
    auto profile_start_init = system_clock::now();
#endif
    q.init();
#ifdef ENABLE_MANUAL_PROFILE
    auto profile_end_init = system_clock::now();
    profile_init = double(duration_cast<microseconds>(profile_end_init - profile_start_init).count()) * microseconds::period::num / microseconds::period::den;
#endif
    cout << "here" << endl;
    int curr_num = 0;
    auto start = system_clock::now();
    // 由于需要定期清空内存，我们在这里记录已生成的猜测总数
    int history = 0;
    // std::ofstream a("./files/results.txt");
    while (!q.priority.empty())
    {
#ifdef ENABLE_MANUAL_PROFILE
        auto profile_start_popnext = system_clock::now();
#endif
        q.PopNext();
#ifdef ENABLE_MANUAL_PROFILE
        auto profile_end_popnext = system_clock::now();
        profile_popnext += double(duration_cast<microseconds>(profile_end_popnext - profile_start_popnext).count()) * microseconds::period::num / microseconds::period::den;
        profile_popnext_calls += 1;
#endif
        q.total_guesses = q.guesses.size();
        if (q.total_guesses - curr_num >= 100000)
        {
            cout << "Guesses generated: " <<history + q.total_guesses << endl;
            curr_num = q.total_guesses;

            // 在此处更改实验生成的猜测上限
            int generate_n=10000000;
            if (history + q.total_guesses > 10000000)
            {
                auto end = system_clock::now();
                auto duration = duration_cast<microseconds>(end - start);
                time_guess = double(duration.count()) * microseconds::period::num / microseconds::period::den;
                cout << "Guess time:" << time_guess - time_hash << "seconds"<< endl;//请不要修改这一行
                cout << "Hash time:" << time_hash << "seconds"<<endl;//请不要修改这一行
                cout << "Train time:" << time_train <<"seconds"<<endl;//请不要修改这一行
#ifdef ENABLE_MANUAL_PROFILE
                const char *profile_path =
#ifdef USE_SERIAL_HASH
                    "serial_manual_profile.tsv";
#else
                    "simd_manual_profile.tsv";
#endif
                ofstream profile(profile_path);
                profile << "item\tseconds\tcalls_or_count\n";
                profile << "model::train\t" << profile_train << "\t1\n";
                profile << "model::order\t" << profile_order << "\t1\n";
                profile << "PriorityQueue::init\t" << profile_init << "\t1\n";
                profile << "PriorityQueue::PopNext\t" << profile_popnext << "\t" << profile_popnext_calls << "\n";
                profile << "hash_batches\t" << time_hash << "\t" << profile_hash_batches << "\n";
                profile << "hashed_passwords\t" << time_hash << "\t" << profile_hashed_passwords << "\n";
                profile << "guess_without_hash\t" << time_guess - time_hash << "\t1\n";
                profile << "total_train_order\t" << time_train << "\t1\n";
                profile << "total_guess_hash\t" << time_guess << "\t1\n";
#endif
                break;
            }
        }
        // 为了避免内存超限，我们在q.guesses中口令达到一定数目时，将其中的所有口令取出并且进行哈希
        // 然后，q.guesses将会被清空。为了有效记录已经生成的口令总数，维护一个history变量来进行记录
        if (curr_num > 1000000)
        {
            auto start_hash = system_clock::now();
            // 输出数组中每个口令占4个bit32（连续存储）
#ifdef USE_SERIAL_HASH
            vector<bit32> simd_states(q.guesses.size() * 4);
            for (size_t i = 0; i < q.guesses.size(); ++i) {
                MD5Hash(q.guesses[i], &simd_states[i * 4]);
            }
#else
            vector<bit32> simd_states;
            MD5HashSIMD(q.guesses, simd_states);
#endif

            // 以下注释部分用于输出猜测和哈希，但是由于自动测试系统不太能写文件，所以这里你可以改成cout
            // for (size_t i = 0; i < q.guesses.size(); ++i)
            // {
            //     a << q.guesses[i] << "\t";
            //     for (int i1 = 0; i1 < 4; i1 += 1)
            //     {
            //         a << std::setw(8) << std::setfill('0') << hex << simd_states[i * 4 + i1];
            //     }
            //     a << endl;
            // }

            // 在这里对哈希所需的总时长进行计算
            auto end_hash = system_clock::now();
            auto duration = duration_cast<microseconds>(end_hash - start_hash);
            time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;
#ifdef ENABLE_MANUAL_PROFILE
            profile_hash_batches += 1;
            profile_hashed_passwords += q.guesses.size();
#endif

            // 记录已经生成的口令总数
            history += curr_num;
            curr_num = 0;
            q.guesses.clear();
        }
    }
}
