#include "md5.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

extern "C" const char *MD5HashCUDARaw(const Byte *flat, int total_bytes,
                                      const int *offsets, const int *lengths,
                                      bit32 *states, int count);
extern "C" const char *MD5HashCUDAPrefixValuesRaw(const Byte *prefix, int prefix_length,
                                                  const Byte *values, int total_value_bytes,
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

void MD5HashCUDAPrefixValues(const string &prefix, const vector<string> &values, vector<bit32> &states)
{
    states.assign(values.size() * 4, 0);
    if (values.empty())
    {
        return;
    }
    if (prefix.size() > static_cast<size_t>(numeric_limits<int>::max()) ||
        values.size() > static_cast<size_t>(numeric_limits<int>::max()))
    {
        throw runtime_error("CUDA direct MD5 batch metadata is too large");
    }

    vector<int> offsets(values.size());
    vector<int> lengths(values.size());
    size_t total_value_bytes = 0;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (values[i].size() > static_cast<size_t>(numeric_limits<int>::max()))
        {
            throw runtime_error("input segment value is too long for CUDA direct MD5 batch");
        }
        offsets[i] = static_cast<int>(total_value_bytes);
        lengths[i] = static_cast<int>(values[i].size());
        total_value_bytes += values[i].size();
        if (total_value_bytes > static_cast<size_t>(numeric_limits<int>::max()))
        {
            throw runtime_error("CUDA direct MD5 batch is too large");
        }
        if (prefix.size() + values[i].size() > static_cast<size_t>(numeric_limits<int>::max()))
        {
            throw runtime_error("generated password is too long for CUDA direct MD5 batch");
        }
    }

    vector<Byte> flat_values(total_value_bytes);
    for (size_t i = 0; i < values.size(); ++i)
    {
        copy(values[i].begin(), values[i].end(), flat_values.begin() + offsets[i]);
    }

    const char *error = MD5HashCUDAPrefixValuesRaw(
        prefix.empty() ? nullptr : reinterpret_cast<const Byte *>(prefix.data()),
        static_cast<int>(prefix.size()),
        flat_values.empty() ? nullptr : flat_values.data(),
        static_cast<int>(flat_values.size()),
        offsets.data(),
        lengths.data(),
        states.data(),
        static_cast<int>(values.size()));
    if (error != nullptr)
    {
        throw runtime_error(string("MD5HashCUDAPrefixValuesRaw failed: ") + error);
    }
}
