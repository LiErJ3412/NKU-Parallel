#include "md5.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

extern "C" const char *MD5HashCUDARaw(const Byte *flat, int total_bytes,
                                      const int *offsets, const int *lengths,
                                      bit32 *states, int count);

void MD5HashCUDA(const vector<string> &inputs, vector<bit32> &states)
{
    states.assign(inputs.size() * 4, 0);
    if (inputs.empty())
    {
        return;
    }
    if (inputs.size() > static_cast<size_t>(numeric_limits<int>::max()))
    {
        throw runtime_error("CUDA MD5 batch has too many passwords");
    }

    vector<int> offsets(inputs.size());
    vector<int> lengths(inputs.size());
    size_t total_bytes = 0;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        if (inputs[i].size() > static_cast<size_t>(numeric_limits<int>::max()))
        {
            throw runtime_error("input password is too long for CUDA MD5 batch");
        }
        offsets[i] = static_cast<int>(total_bytes);
        lengths[i] = static_cast<int>(inputs[i].size());
        total_bytes += inputs[i].size();
        if (total_bytes > static_cast<size_t>(numeric_limits<int>::max()))
        {
            throw runtime_error("CUDA MD5 batch is too large; reduce PCFG_HASH_BATCH");
        }
    }

    vector<Byte> flat(total_bytes);
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        copy(inputs[i].begin(), inputs[i].end(), flat.begin() + offsets[i]);
    }

    const char *error = MD5HashCUDARaw(flat.empty() ? nullptr : flat.data(),
                                      static_cast<int>(flat.size()),
                                      offsets.data(),
                                      lengths.data(),
                                      states.data(),
                                      static_cast<int>(inputs.size()));
    if (error != nullptr)
    {
        throw runtime_error(string("MD5HashCUDARaw failed: ") + error);
    }
}
