#include "PCFG.h"
#include "md5.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef USE_MPI
#include "mpi.h"
#endif

using namespace std;
using namespace chrono;

namespace
{
const char *kTrainPath = "../rockyou.txt";

int ReadEnvInt(const char *name, int default_value)
{
    const char *value = getenv(name);
    if (value == nullptr)
    {
        return default_value;
    }
    const int parsed = atoi(value);
    return parsed > 0 ? parsed : default_value;
}

bool ReadEnvBool(const char *name, bool default_value)
{
    const char *value = getenv(name);
    if (value == nullptr)
    {
        return default_value;
    }
    string parsed = value;
    return !(parsed == "0" || parsed == "false" || parsed == "off" || parsed == "no");
}

GenerateMode ReadGenerateMode()
{
    const char *value = getenv("PCFG_GENERATE_MODE");
    if (value == nullptr)
    {
        return GenerateMode::Pthread;
    }
    string mode = value;
    if (mode == "serial")
    {
        return GenerateMode::Serial;
    }
    if (mode == "openmp")
    {
        return GenerateMode::OpenMP;
    }
    return GenerateMode::Pthread;
}

string GenerateModeName(GenerateMode mode)
{
    if (mode == GenerateMode::Serial)
    {
        return "serial";
    }
    if (mode == GenerateMode::OpenMP)
    {
        return "openmp";
    }
    return "pthread";
}

double SecondsSince(system_clock::time_point start, system_clock::time_point end)
{
    return double(duration_cast<microseconds>(end - start).count()) *
           microseconds::period::num / microseconds::period::den;
}

void HashGuesses(const vector<string> &guesses)
{
#ifdef USE_CUDA_HASH
    vector<bit32> states;
    MD5HashCUDA(guesses, states);
#elif defined(USE_SERIAL_HASH)
    vector<bit32> states(guesses.size() * 4);
    for (size_t i = 0; i < guesses.size(); ++i)
    {
        MD5Hash(guesses[i], &states[i * 4]);
    }
#else
    vector<bit32> states;
    MD5HashSIMD(guesses, states);
#endif
}

bool RunMD5SelfTest(bool verbose)
{
    if (verbose)
    {
        cout << "Testing MD5Hash correctness..." << endl;
    }
    string test_pws[8] = {"123456", "password", "12345678", "qwerty", "123456789", "12345", "1234", "111111"};
    string test_hashes[8] = {
        "e10adc3949ba59abbe56e057f20f883e",
        "5f4dcc3b5aa765d61d8327deb882cf99",
        "25d55ad283aa400af464c76d713c07ad",
        "d8578edf8458ce06fbc5bb76a58c5ca4",
        "25f9e794323b453885f5181f1b624d0b",
        "827ccb0eea8a706c4c34a16891f84e7b",
        "81dc9bdb52d04dc20036dbd8313ed055",
        "96e79218965eb72c92a549dd5a330112"};
    for (int i = 0; i < 8; i++)
    {
        bit32 state[4];
        MD5Hash(test_pws[i], state);
        stringstream ss;
        for (int i1 = 0; i1 < 4; i1 += 1)
        {
            ss << setw(8) << setfill('0') << hex << state[i1];
        }
        if (ss.str() != test_hashes[i])
        {
            if (verbose)
            {
                cout << "MD5Hash test failed for " << test_pws[i] << "!" << endl;
                cout << "Expected: " << test_hashes[i] << "\nGot:      " << ss.str() << endl;
            }
            return false;
        }
    }
#ifdef USE_CUDA_HASH
    vector<string> cuda_inputs(test_pws, test_pws + 8);
    cuda_inputs.emplace_back("bvaisdbjasdkafkasdfnavkjnakdjfejfanjsdnfkajdfkajdfjkwanfdjaknsvjkanbjbjadfajwefajksdfakdnsvjadfasjdva");
    vector<bit32> cuda_states;
    MD5HashCUDA(cuda_inputs, cuda_states);
    for (size_t i = 0; i < cuda_inputs.size(); i++)
    {
        bit32 expected_state[4];
        MD5Hash(cuda_inputs[i], expected_state);
        for (int word = 0; word < 4; word += 1)
        {
            if (cuda_states[i * 4 + word] != expected_state[word])
            {
                if (verbose)
                {
                    cout << "MD5HashCUDA test failed for " << cuda_inputs[i] << "!" << endl;
                }
                return false;
            }
        }
    }
#endif
    if (verbose)
    {
        cout << "MD5Hash test passed!" << endl; //请不要修改这一行
    }
    return true;
}

void ConfigureQueue(PriorityQueue &q)
{
    const int hw_threads = max(1u, thread::hardware_concurrency());
    const GenerateMode generate_mode = ReadGenerateMode();
    const int generate_threads = ReadEnvInt("PCFG_THREADS", hw_threads);
    const int generate_threshold = ReadEnvInt("PCFG_GENERATE_THRESHOLD", 50000);
    q.ConfigureGeneration(generate_mode, generate_threads, generate_threshold);
    cout << "Generate mode:" << GenerateModeName(generate_mode)
         << " threads:" << generate_threads
         << " threshold:" << generate_threshold << endl;
}

void InsertPTByProb(PriorityQueue &q, PT pt)
{
    q.CalProb(pt);
    if (q.priority.empty())
    {
        q.priority.emplace_back(pt);
        return;
    }
    for (auto iter = q.priority.begin(); iter != q.priority.end(); iter++)
    {
        if (iter != q.priority.end() - 1 && iter != q.priority.begin())
        {
            if (pt.prob <= iter->prob && pt.prob > (iter + 1)->prob)
            {
                q.priority.emplace(iter + 1, pt);
                return;
            }
        }
        if (iter == q.priority.end() - 1)
        {
            q.priority.emplace_back(pt);
            return;
        }
        if (iter == q.priority.begin() && iter->prob < pt.prob)
        {
            q.priority.emplace(iter, pt);
            return;
        }
    }
}

void ProcessLocalTasks(PriorityQueue &q, const vector<PT> &tasks, bool enable_hash,
                       long long &generated, double &hash_time)
{
    generated = 0;
    hash_time = 0;
    for (const PT &task : tasks)
    {
        const size_t before = q.guesses.size();
        q.Generate(task);
        generated += static_cast<long long>(q.guesses.size() - before);
        if (enable_hash && q.guesses.size() >= static_cast<size_t>(ReadEnvInt("PCFG_HASH_BATCH", 1000000)))
        {
            auto start_hash = system_clock::now();
            HashGuesses(q.guesses);
            auto end_hash = system_clock::now();
            hash_time += SecondsSince(start_hash, end_hash);
            q.guesses.clear();
        }
    }
    if (enable_hash && !q.guesses.empty())
    {
        auto start_hash = system_clock::now();
        HashGuesses(q.guesses);
        auto end_hash = system_clock::now();
        hash_time += SecondsSince(start_hash, end_hash);
        q.guesses.clear();
    }
    if (!enable_hash)
    {
        q.guesses.clear();
    }
}

void RunSerial()
{
    if (!RunMD5SelfTest(true))
    {
        return;
    }

    double time_hash = 0;
    double time_guess = 0;
    double time_train = 0;
    PriorityQueue q;
    ConfigureQueue(q);
    const int generate_limit = ReadEnvInt("PCFG_GUESS_LIMIT", 10000000);
    const int hash_batch = ReadEnvInt("PCFG_HASH_BATCH", 1000000);

    auto start_train = system_clock::now();
    q.m.train(kTrainPath);
    q.m.order();
    auto end_train = system_clock::now();
    time_train = SecondsSince(start_train, end_train);

    q.init();
    cout << "here" << endl;
    int curr_num = 0;
    int history = 0;
    auto start = system_clock::now();
    while (!q.priority.empty())
    {
        q.PopNext();
        q.total_guesses = q.guesses.size();
        if (q.total_guesses - curr_num >= 100000)
        {
            cout << "Guesses generated: " << history + q.total_guesses << endl;
            curr_num = q.total_guesses;
            if (history + q.total_guesses > generate_limit)
            {
                auto end = system_clock::now();
                time_guess = SecondsSince(start, end);
                cout << "Guess time:" << time_guess - time_hash << "seconds" << endl; //请不要修改这一行
                cout << "Hash time:" << time_hash << "seconds" << endl;              //请不要修改这一行
                cout << "Train time:" << time_train << "seconds" << endl;             //请不要修改这一行
                break;
            }
        }
        if (curr_num > hash_batch)
        {
            auto start_hash = system_clock::now();
            HashGuesses(q.guesses);
            auto end_hash = system_clock::now();
            time_hash += SecondsSince(start_hash, end_hash);
            history += curr_num;
            curr_num = 0;
            q.guesses.clear();
        }
    }
}

#ifdef USE_MPI
vector<int> PackPT(const PT &pt)
{
    vector<int> packed;
    packed.emplace_back(static_cast<int>(pt.content.size()));
    packed.emplace_back(pt.pivot);
    for (const segment &seg : pt.content)
    {
        packed.emplace_back(seg.type);
        packed.emplace_back(seg.length);
    }
    packed.emplace_back(static_cast<int>(pt.curr_indices.size()));
    packed.insert(packed.end(), pt.curr_indices.begin(), pt.curr_indices.end());
    packed.emplace_back(static_cast<int>(pt.max_indices.size()));
    packed.insert(packed.end(), pt.max_indices.begin(), pt.max_indices.end());
    return packed;
}

PT UnpackPT(const vector<int> &packed)
{
    PT pt;
    size_t pos = 0;
    const int content_size = packed[pos++];
    pt.pivot = packed[pos++];
    for (int i = 0; i < content_size; ++i)
    {
        const int type = packed[pos++];
        const int length = packed[pos++];
        pt.content.emplace_back(type, length);
    }
    const int curr_size = packed[pos++];
    for (int i = 0; i < curr_size; ++i)
    {
        pt.curr_indices.emplace_back(packed[pos++]);
    }
    const int max_size = packed[pos++];
    for (int i = 0; i < max_size; ++i)
    {
        pt.max_indices.emplace_back(packed[pos++]);
    }
    return pt;
}

void SendIntVector(const vector<int> &data, int dest, int tag)
{
    const int count = static_cast<int>(data.size());
    MPI_Send(&count, 1, MPI_INT, dest, tag, MPI_COMM_WORLD);
    if (count > 0)
    {
        MPI_Send(data.data(), count, MPI_INT, dest, tag + 1, MPI_COMM_WORLD);
    }
}

vector<int> RecvIntVector(int source, int tag)
{
    int count = 0;
    MPI_Recv(&count, 1, MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    vector<int> data(count);
    if (count > 0)
    {
        MPI_Recv(data.data(), count, MPI_INT, source, tag + 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    return data;
}

vector<int> PackPTList(const vector<PT> &pts)
{
    vector<int> packed;
    packed.emplace_back(static_cast<int>(pts.size()));
    for (const PT &pt : pts)
    {
        vector<int> one = PackPT(pt);
        packed.emplace_back(static_cast<int>(one.size()));
        packed.insert(packed.end(), one.begin(), one.end());
    }
    return packed;
}

vector<PT> UnpackPTList(const vector<int> &packed)
{
    vector<PT> pts;
    if (packed.empty())
    {
        return pts;
    }
    size_t pos = 0;
    const int count = packed[pos++];
    pts.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        const int item_size = packed[pos++];
        vector<int> one(packed.begin() + pos, packed.begin() + pos + item_size);
        pts.emplace_back(UnpackPT(one));
        pos += item_size;
    }
    return pts;
}

void BroadcastSegmentValues(vector<segment> &segments, int root)
{
    int segment_count = static_cast<int>(segments.size());
    MPI_Bcast(&segment_count, 1, MPI_INT, root, MPI_COMM_WORLD);
    if (segment_count == 0)
    {
        segments.clear();
        return;
    }
    if (segments.empty())
    {
        segments.assign(segment_count, segment(0, 0));
    }
    for (int i = 0; i < segment_count; ++i)
    {
        MPI_Bcast(&segments[i].type, 1, MPI_INT, root, MPI_COMM_WORLD);
        MPI_Bcast(&segments[i].length, 1, MPI_INT, root, MPI_COMM_WORLD);
        int value_count = static_cast<int>(segments[i].ordered_values.size());
        MPI_Bcast(&value_count, 1, MPI_INT, root, MPI_COMM_WORLD);
        if (segments[i].ordered_values.empty())
        {
            segments[i].ordered_values.resize(value_count);
        }
        for (int j = 0; j < value_count; ++j)
        {
            int len = static_cast<int>(segments[i].ordered_values[j].size());
            MPI_Bcast(&len, 1, MPI_INT, root, MPI_COMM_WORLD);
            if (segments[i].ordered_values[j].size() != static_cast<size_t>(len))
            {
                segments[i].ordered_values[j].resize(len);
            }
            if (len > 0)
            {
                MPI_Bcast(&segments[i].ordered_values[j][0], len, MPI_CHAR, root, MPI_COMM_WORLD);
            }
        }
    }
}

void BroadcastModelForGenerate(PriorityQueue &q, int root)
{
    BroadcastSegmentValues(q.m.letters, root);
    BroadcastSegmentValues(q.m.digits, root);
    BroadcastSegmentValues(q.m.symbols, root);
}

void SendTasks(const vector<PT> &tasks, int dest)
{
    SendIntVector(PackPTList(tasks), dest, 100);
}

vector<PT> RecvTasks(int source)
{
    return UnpackPTList(RecvIntVector(source, 100));
}

void SendWorkerResult(long long generated, double hash_time, int dest)
{
    MPI_Send(&generated, 1, MPI_LONG_LONG, dest, 200, MPI_COMM_WORLD);
    MPI_Send(&hash_time, 1, MPI_DOUBLE, dest, 201, MPI_COMM_WORLD);
}

void RecvWorkerResult(int source, long long &generated, double &hash_time)
{
    MPI_Recv(&generated, 1, MPI_LONG_LONG, source, 200, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&hash_time, 1, MPI_DOUBLE, source, 201, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

void InsertGeneratedPTs(PriorityQueue &q, const vector<PT> &new_pts)
{
    for (PT pt : new_pts)
    {
        InsertPTByProb(q, pt);
    }
}

long long EstimatePTGuesses(const PT &pt)
{
    if (pt.max_indices.empty())
    {
        return 0;
    }
    return static_cast<long long>(pt.max_indices.back());
}

void RunMPI(int rank, int size)
{
    if (!RunMD5SelfTest(rank == 0))
    {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    PriorityQueue q;
    const int hw_threads = max(1u, thread::hardware_concurrency());
    const GenerateMode generate_mode = ReadGenerateMode();
    const int generate_threads = ReadEnvInt("PCFG_THREADS", hw_threads);
    const int generate_threshold = ReadEnvInt("PCFG_GENERATE_THRESHOLD", 50000);
    q.ConfigureGeneration(generate_mode, generate_threads, generate_threshold);

    const int generate_limit = ReadEnvInt("PCFG_GUESS_LIMIT", 10000000);
    const int mpi_batch = max(size, ReadEnvInt("PCFG_MPI_BATCH", size * 4));
    const int mpi_batch_guess_limit = ReadEnvInt("PCFG_MPI_BATCH_GUESS_LIMIT", 1000000);
    const bool mpi_hash = ReadEnvBool("PCFG_MPI_HASH", true);
    double time_train = 0;
    double time_hash = 0;
    double comm_time = 0;

    if (rank == 0)
    {
        cout << "Generate mode:" << GenerateModeName(generate_mode)
             << " threads:" << generate_threads
             << " threshold:" << generate_threshold << endl;
        cout << "MPI size:" << size << " batch:" << mpi_batch
             << " batch_guess_limit:" << mpi_batch_guess_limit
             << " hash:" << (mpi_hash ? "on" : "off") << endl;
        auto start_train = system_clock::now();
        q.m.train(kTrainPath);
        q.m.order();
        q.init();
        auto end_train = system_clock::now();
        time_train = SecondsSince(start_train, end_train);
    }

    BroadcastModelForGenerate(q, 0);
    MPI_Barrier(MPI_COMM_WORLD);
    auto start_guess = system_clock::now();

    long long total_generated = 0;
    bool done = false;
    while (!done)
    {
        vector<vector<PT>> assigned(size);
        if (rank == 0)
        {
            vector<PT> batch_new_pts;
            vector<PT> batch;
            long long batch_guess_estimate = 0;
            while (!q.priority.empty() && static_cast<int>(batch.size()) < mpi_batch &&
                   total_generated < generate_limit)
            {
                PT current = q.priority.front();
                const long long current_estimate = EstimatePTGuesses(current);
                if (!batch.empty() && batch_guess_estimate + current_estimate > mpi_batch_guess_limit)
                {
                    break;
                }
                q.priority.erase(q.priority.begin());
                batch_guess_estimate += current_estimate;
                vector<PT> new_pts = current.NewPTs();
                batch_new_pts.insert(batch_new_pts.end(), new_pts.begin(), new_pts.end());
                batch.emplace_back(current);
            }
            if (batch.empty())
            {
                done = true;
            }
            else
            {
                for (size_t i = 0; i < batch.size(); ++i)
                {
                    assigned[i % size].emplace_back(batch[i]);
                }
                InsertGeneratedPTs(q, batch_new_pts);
            }
        }

        int done_int = done ? 1 : 0;
        MPI_Bcast(&done_int, 1, MPI_INT, 0, MPI_COMM_WORLD);
        done = done_int != 0;
        if (done)
        {
            break;
        }

        auto start_comm = system_clock::now();
        if (rank == 0)
        {
            for (int dest = 1; dest < size; ++dest)
            {
                SendTasks(assigned[dest], dest);
            }
        }
        else
        {
            assigned[rank] = RecvTasks(0);
        }
        auto end_comm = system_clock::now();
        comm_time += SecondsSince(start_comm, end_comm);

        long long local_generated = 0;
        double local_hash_time = 0;
        ProcessLocalTasks(q, assigned[rank], mpi_hash, local_generated, local_hash_time);

        start_comm = system_clock::now();
        if (rank == 0)
        {
            double round_hash_time = local_hash_time;
            const long long before_round = total_generated;
            total_generated += local_generated;
            for (int source = 1; source < size; ++source)
            {
                long long worker_generated = 0;
                double worker_hash_time = 0;
                RecvWorkerResult(source, worker_generated, worker_hash_time);
                total_generated += worker_generated;
                round_hash_time = max(round_hash_time, worker_hash_time);
            }
            time_hash += round_hash_time;
            if (total_generated / 100000 > before_round / 100000)
            {
                cout << "Guesses generated: " << total_generated << endl;
            }
            if (total_generated >= generate_limit)
            {
                done = true;
            }
        }
        else
        {
            SendWorkerResult(local_generated, local_hash_time, 0);
        }
        end_comm = system_clock::now();
        comm_time += SecondsSince(start_comm, end_comm);

        int continue_int = done ? 0 : 1;
        MPI_Bcast(&continue_int, 1, MPI_INT, 0, MPI_COMM_WORLD);
        done = continue_int == 0;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    auto end_guess = system_clock::now();
    if (rank == 0)
    {
        const double wall = SecondsSince(start_guess, end_guess);
        cout << "MPI generated guesses:" << total_generated << endl;
        cout << "MPI communication time:" << comm_time << "seconds" << endl;
        cout << "Guess time:" << wall - time_hash << "seconds" << endl; //请不要修改这一行
        cout << "Hash time:" << time_hash << "seconds" << endl;         //请不要修改这一行
        cout << "Train time:" << time_train << "seconds" << endl;        //请不要修改这一行
    }
}
#endif
}

int main(int argc, char **argv)
{
#ifdef USE_MPI
    MPI_Init(&argc, &argv);
    int rank = 0;
    int size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    RunMPI(rank, size);
    MPI_Finalize();
    return 0;
#else
    RunSerial();
    return 0;
#endif
}
