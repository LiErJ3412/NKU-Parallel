#include <cuda_runtime.h>

typedef unsigned char Byte;
typedef unsigned int bit32;

#define s11 7
#define s12 12
#define s13 17
#define s14 22
#define s21 5
#define s22 9
#define s23 14
#define s24 20
#define s31 4
#define s32 11
#define s33 16
#define s34 23
#define s41 6
#define s42 10
#define s43 15
#define s44 21

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
#define ROTATELEFT(num, n) (((num) << (n)) | ((num) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac) \
    {                             \
        (a) += F((b), (c), (d)) + (x) + ac; \
        (a) = ROTATELEFT((a), (s));         \
        (a) += (b);                         \
    }
#define GG(a, b, c, d, x, s, ac) \
    {                             \
        (a) += G((b), (c), (d)) + (x) + ac; \
        (a) = ROTATELEFT((a), (s));         \
        (a) += (b);                         \
    }
#define HH(a, b, c, d, x, s, ac) \
    {                             \
        (a) += H((b), (c), (d)) + (x) + ac; \
        (a) = ROTATELEFT((a), (s));         \
        (a) += (b);                         \
    }
#define II(a, b, c, d, x, s, ac) \
    {                             \
        (a) += I((b), (c), (d)) + (x) + ac; \
        (a) = ROTATELEFT((a), (s));         \
        (a) += (b);                         \
    }

namespace
{
const int kThreadsPerBlock = 256;

__host__ __device__ inline bit32 ByteSwap32CUDA(bit32 value)
{
    return ((value & 0xffU) << 24) |
           ((value & 0xff00U) << 8) |
           ((value & 0xff0000U) >> 8) |
           ((value & 0xff000000U) >> 24);
}

__device__ inline bit32 LoadWordLECUDA(const Byte *src)
{
    return static_cast<bit32>(src[0]) |
           (static_cast<bit32>(src[1]) << 8) |
           (static_cast<bit32>(src[2]) << 16) |
           (static_cast<bit32>(src[3]) << 24);
}

__device__ inline void MD5TransformCUDA(const bit32 x[16], bit32 &a, bit32 &b, bit32 &c, bit32 &d)
{
    FF(a, b, c, d, x[0], s11, 0xd76aa478);
    FF(d, a, b, c, x[1], s12, 0xe8c7b756);
    FF(c, d, a, b, x[2], s13, 0x242070db);
    FF(b, c, d, a, x[3], s14, 0xc1bdceee);
    FF(a, b, c, d, x[4], s11, 0xf57c0faf);
    FF(d, a, b, c, x[5], s12, 0x4787c62a);
    FF(c, d, a, b, x[6], s13, 0xa8304613);
    FF(b, c, d, a, x[7], s14, 0xfd469501);
    FF(a, b, c, d, x[8], s11, 0x698098d8);
    FF(d, a, b, c, x[9], s12, 0x8b44f7af);
    FF(c, d, a, b, x[10], s13, 0xffff5bb1);
    FF(b, c, d, a, x[11], s14, 0x895cd7be);
    FF(a, b, c, d, x[12], s11, 0x6b901122);
    FF(d, a, b, c, x[13], s12, 0xfd987193);
    FF(c, d, a, b, x[14], s13, 0xa679438e);
    FF(b, c, d, a, x[15], s14, 0x49b40821);

    GG(a, b, c, d, x[1], s21, 0xf61e2562);
    GG(d, a, b, c, x[6], s22, 0xc040b340);
    GG(c, d, a, b, x[11], s23, 0x265e5a51);
    GG(b, c, d, a, x[0], s24, 0xe9b6c7aa);
    GG(a, b, c, d, x[5], s21, 0xd62f105d);
    GG(d, a, b, c, x[10], s22, 0x2441453);
    GG(c, d, a, b, x[15], s23, 0xd8a1e681);
    GG(b, c, d, a, x[4], s24, 0xe7d3fbc8);
    GG(a, b, c, d, x[9], s21, 0x21e1cde6);
    GG(d, a, b, c, x[14], s22, 0xc33707d6);
    GG(c, d, a, b, x[3], s23, 0xf4d50d87);
    GG(b, c, d, a, x[8], s24, 0x455a14ed);
    GG(a, b, c, d, x[13], s21, 0xa9e3e905);
    GG(d, a, b, c, x[2], s22, 0xfcefa3f8);
    GG(c, d, a, b, x[7], s23, 0x676f02d9);
    GG(b, c, d, a, x[12], s24, 0x8d2a4c8a);

    HH(a, b, c, d, x[5], s31, 0xfffa3942);
    HH(d, a, b, c, x[8], s32, 0x8771f681);
    HH(c, d, a, b, x[11], s33, 0x6d9d6122);
    HH(b, c, d, a, x[14], s34, 0xfde5380c);
    HH(a, b, c, d, x[1], s31, 0xa4beea44);
    HH(d, a, b, c, x[4], s32, 0x4bdecfa9);
    HH(c, d, a, b, x[7], s33, 0xf6bb4b60);
    HH(b, c, d, a, x[10], s34, 0xbebfbc70);
    HH(a, b, c, d, x[13], s31, 0x289b7ec6);
    HH(d, a, b, c, x[0], s32, 0xeaa127fa);
    HH(c, d, a, b, x[3], s33, 0xd4ef3085);
    HH(b, c, d, a, x[6], s34, 0x4881d05);
    HH(a, b, c, d, x[9], s31, 0xd9d4d039);
    HH(d, a, b, c, x[12], s32, 0xe6db99e5);
    HH(c, d, a, b, x[15], s33, 0x1fa27cf8);
    HH(b, c, d, a, x[2], s34, 0xc4ac5665);

    II(a, b, c, d, x[0], s41, 0xf4292244);
    II(d, a, b, c, x[7], s42, 0x432aff97);
    II(c, d, a, b, x[14], s43, 0xab9423a7);
    II(b, c, d, a, x[5], s44, 0xfc93a039);
    II(a, b, c, d, x[12], s41, 0x655b59c3);
    II(d, a, b, c, x[3], s42, 0x8f0ccc92);
    II(c, d, a, b, x[10], s43, 0xffeff47d);
    II(b, c, d, a, x[1], s44, 0x85845dd1);
    II(a, b, c, d, x[8], s41, 0x6fa87e4f);
    II(d, a, b, c, x[15], s42, 0xfe2ce6e0);
    II(c, d, a, b, x[6], s43, 0xa3014314);
    II(b, c, d, a, x[13], s44, 0x4e0811a1);
    II(a, b, c, d, x[4], s41, 0xf7537e82);
    II(d, a, b, c, x[11], s42, 0xbd3af235);
    II(c, d, a, b, x[2], s43, 0x2ad7d2bb);
    II(b, c, d, a, x[9], s44, 0xeb86d391);
}

__global__ void MD5HashKernel(const Byte *data, const int *offsets, const int *lengths,
                              bit32 *states, int count)
{
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count)
    {
        return;
    }

    const Byte *input = data + offsets[idx];
    const int length = lengths[idx];
    const unsigned long long bit_length = static_cast<unsigned long long>(length) * 8ULL;
    const int padding_bits_mod = (length * 8) % 512;
    int padding_bits = 0;
    if (padding_bits_mod > 448)
    {
        padding_bits = 512 - (padding_bits_mod - 448);
    }
    else if (padding_bits_mod < 448)
    {
        padding_bits = 448 - padding_bits_mod;
    }
    else
    {
        padding_bits = 512;
    }
    const int padded_length = length + padding_bits / 8 + 8;
    const int block_count = padded_length / 64;

    bit32 state0 = 0x67452301;
    bit32 state1 = 0xefcdab89;
    bit32 state2 = 0x98badcfe;
    bit32 state3 = 0x10325476;

    for (int block = 0; block < block_count; ++block)
    {
        Byte block_bytes[64];
        const int block_start = block * 64;
        for (int i = 0; i < 64; ++i)
        {
            const int pos = block_start + i;
            Byte value = 0;
            if (pos < length)
            {
                value = input[pos];
            }
            else if (pos == length)
            {
                value = 0x80;
            }
            else if (pos >= padded_length - 8)
            {
                const int shift = (pos - (padded_length - 8)) * 8;
                value = static_cast<Byte>((bit_length >> shift) & 0xffU);
            }
            block_bytes[i] = value;
        }

        bit32 x[16];
        for (int i = 0; i < 16; ++i)
        {
            x[i] = LoadWordLECUDA(&block_bytes[i * 4]);
        }

        bit32 a = state0;
        bit32 b = state1;
        bit32 c = state2;
        bit32 d = state3;
        MD5TransformCUDA(x, a, b, c, d);

        state0 += a;
        state1 += b;
        state2 += c;
        state3 += d;
    }

    states[idx * 4 + 0] = ByteSwap32CUDA(state0);
    states[idx * 4 + 1] = ByteSwap32CUDA(state1);
    states[idx * 4 + 2] = ByteSwap32CUDA(state2);
    states[idx * 4 + 3] = ByteSwap32CUDA(state3);
}
}

extern "C" const char *MD5HashCUDARaw(const Byte *flat, int total_bytes,
                                      const int *offsets, const int *lengths,
                                      bit32 *states, int count)
{
    Byte *d_data = 0;
    int *d_offsets = 0;
    int *d_lengths = 0;
    bit32 *d_states = 0;

    const int data_bytes = total_bytes > 0 ? total_bytes : 1;
    cudaError_t status = cudaMalloc(&d_data, static_cast<size_t>(data_bytes));
    if (status != cudaSuccess)
    {
        return cudaGetErrorString(status);
    }
    if (total_bytes > 0)
    {
        status = cudaMemcpy(d_data, flat, static_cast<size_t>(total_bytes), cudaMemcpyHostToDevice);
        if (status != cudaSuccess)
        {
            cudaFree(d_data);
            return cudaGetErrorString(status);
        }
    }

    status = cudaMalloc(&d_offsets, static_cast<size_t>(count) * sizeof(int));
    if (status != cudaSuccess)
    {
        cudaFree(d_data);
        return cudaGetErrorString(status);
    }
    status = cudaMalloc(&d_lengths, static_cast<size_t>(count) * sizeof(int));
    if (status != cudaSuccess)
    {
        cudaFree(d_data);
        cudaFree(d_offsets);
        return cudaGetErrorString(status);
    }
    status = cudaMalloc(&d_states, static_cast<size_t>(count) * 4 * sizeof(bit32));
    if (status != cudaSuccess)
    {
        cudaFree(d_data);
        cudaFree(d_offsets);
        cudaFree(d_lengths);
        return cudaGetErrorString(status);
    }

    status = cudaMemcpy(d_offsets, offsets, static_cast<size_t>(count) * sizeof(int), cudaMemcpyHostToDevice);
    if (status == cudaSuccess)
    {
        status = cudaMemcpy(d_lengths, lengths, static_cast<size_t>(count) * sizeof(int), cudaMemcpyHostToDevice);
    }
    if (status == cudaSuccess)
    {
        const int blocks = (count + kThreadsPerBlock - 1) / kThreadsPerBlock;
        MD5HashKernel<<<blocks, kThreadsPerBlock>>>(d_data, d_offsets, d_lengths, d_states, count);
        status = cudaGetLastError();
    }
    if (status == cudaSuccess)
    {
        status = cudaDeviceSynchronize();
    }
    if (status == cudaSuccess)
    {
        status = cudaMemcpy(states, d_states, static_cast<size_t>(count) * 4 * sizeof(bit32),
                            cudaMemcpyDeviceToHost);
    }

    cudaFree(d_data);
    cudaFree(d_offsets);
    cudaFree(d_lengths);
    cudaFree(d_states);

    if (status != cudaSuccess)
    {
        return cudaGetErrorString(status);
    }
    return 0;
}
