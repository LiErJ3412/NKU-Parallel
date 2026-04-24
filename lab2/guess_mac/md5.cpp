#include "md5.h"
#include <algorithm>
#include <cassert>
#include <stdint.h>
#include <cstring>

using namespace std;

namespace
{
// 4x32-bit向量：每个lane对应一个不同口令，进行同构的MD5位运算
typedef uint32_t bit32x4 __attribute__((vector_size(16)));

inline bit32x4 MakeVec4(bit32 v0, bit32 v1, bit32 v2, bit32 v3)
{
	bit32x4 v;
	v[0] = v0;
	v[1] = v1;
	v[2] = v2;
	v[3] = v3;
	return v;
}

inline bit32x4 Broadcast4(bit32 v)
{
	return MakeVec4(v, v, v, v);
}

inline bit32x4 RotateLeft4(bit32x4 num, int n)
{
	// lane内循环左移，4个口令同步执行
	return (num << n) | (num >> (32 - n));
}

inline bit32x4 F4(bit32x4 x, bit32x4 y, bit32x4 z)
{
	return ((x & y) | ((~x) & z));
}

inline bit32x4 G4(bit32x4 x, bit32x4 y, bit32x4 z)
{
	return ((x & z) | (y & (~z)));
}

inline bit32x4 H4(bit32x4 x, bit32x4 y, bit32x4 z)
{
	return (x ^ y ^ z);
}

inline bit32x4 I4(bit32x4 x, bit32x4 y, bit32x4 z)
{
	return (y ^ (x | (~z)));
}

inline void FF4(bit32x4 &a, const bit32x4 &b, const bit32x4 &c, const bit32x4 &d, const bit32x4 &x, int s, bit32 ac)
{
	a += F4(b, c, d) + x + Broadcast4(ac);
	a = RotateLeft4(a, s);
	a += b;
}

inline void GG4(bit32x4 &a, const bit32x4 &b, const bit32x4 &c, const bit32x4 &d, const bit32x4 &x, int s, bit32 ac)
{
	a += G4(b, c, d) + x + Broadcast4(ac);
	a = RotateLeft4(a, s);
	a += b;
}

inline void HH4(bit32x4 &a, const bit32x4 &b, const bit32x4 &c, const bit32x4 &d, const bit32x4 &x, int s, bit32 ac)
{
	a += H4(b, c, d) + x + Broadcast4(ac);
	a = RotateLeft4(a, s);
	a += b;
}

inline void II4(bit32x4 &a, const bit32x4 &b, const bit32x4 &c, const bit32x4 &d, const bit32x4 &x, int s, bit32 ac)
{
	a += I4(b, c, d) + x + Broadcast4(ac);
	a = RotateLeft4(a, s);
	a += b;
}

inline bit32 LoadWordLE(const Byte *src)
{
	return static_cast<bit32>(src[0]) |
		   (static_cast<bit32>(src[1]) << 8) |
		   (static_cast<bit32>(src[2]) << 16) |
		   (static_cast<bit32>(src[3]) << 24);
}

inline bit32 ByteSwap32(bit32 value)
{
	return ((value & 0xffU) << 24) |
		   ((value & 0xff00U) << 8) |
		   ((value & 0xff0000U) >> 8) |
		   ((value & 0xff000000U) >> 24);
}

inline void StoreVec4(bit32x4 v, bit32 out[4])
{
	memcpy(out, &v, sizeof(bit32) * 4);
}

void MD5RoundsScalar(const bit32 x[16], bit32 &a, bit32 &b, bit32 &c, bit32 &d)
{
	// 与原始实现一致的标量4轮流程，保证单口令接口兼容
	/* Round 1 */
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

	/* Round 2 */
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

	/* Round 3 */
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

	/* Round 4 */
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

void MD5RoundsSIMD(const bit32x4 x[16], bit32x4 &a, bit32x4 &b, bit32x4 &c, bit32x4 &d)
{
	// SIMD版本4轮流程：FF/GG/HH/II在4个lane上并行执行
	/* Round 1 */
	FF4(a, b, c, d, x[0], s11, 0xd76aa478);
	FF4(d, a, b, c, x[1], s12, 0xe8c7b756);
	FF4(c, d, a, b, x[2], s13, 0x242070db);
	FF4(b, c, d, a, x[3], s14, 0xc1bdceee);
	FF4(a, b, c, d, x[4], s11, 0xf57c0faf);
	FF4(d, a, b, c, x[5], s12, 0x4787c62a);
	FF4(c, d, a, b, x[6], s13, 0xa8304613);
	FF4(b, c, d, a, x[7], s14, 0xfd469501);
	FF4(a, b, c, d, x[8], s11, 0x698098d8);
	FF4(d, a, b, c, x[9], s12, 0x8b44f7af);
	FF4(c, d, a, b, x[10], s13, 0xffff5bb1);
	FF4(b, c, d, a, x[11], s14, 0x895cd7be);
	FF4(a, b, c, d, x[12], s11, 0x6b901122);
	FF4(d, a, b, c, x[13], s12, 0xfd987193);
	FF4(c, d, a, b, x[14], s13, 0xa679438e);
	FF4(b, c, d, a, x[15], s14, 0x49b40821);

	/* Round 2 */
	GG4(a, b, c, d, x[1], s21, 0xf61e2562);
	GG4(d, a, b, c, x[6], s22, 0xc040b340);
	GG4(c, d, a, b, x[11], s23, 0x265e5a51);
	GG4(b, c, d, a, x[0], s24, 0xe9b6c7aa);
	GG4(a, b, c, d, x[5], s21, 0xd62f105d);
	GG4(d, a, b, c, x[10], s22, 0x2441453);
	GG4(c, d, a, b, x[15], s23, 0xd8a1e681);
	GG4(b, c, d, a, x[4], s24, 0xe7d3fbc8);
	GG4(a, b, c, d, x[9], s21, 0x21e1cde6);
	GG4(d, a, b, c, x[14], s22, 0xc33707d6);
	GG4(c, d, a, b, x[3], s23, 0xf4d50d87);
	GG4(b, c, d, a, x[8], s24, 0x455a14ed);
	GG4(a, b, c, d, x[13], s21, 0xa9e3e905);
	GG4(d, a, b, c, x[2], s22, 0xfcefa3f8);
	GG4(c, d, a, b, x[7], s23, 0x676f02d9);
	GG4(b, c, d, a, x[12], s24, 0x8d2a4c8a);

	/* Round 3 */
	HH4(a, b, c, d, x[5], s31, 0xfffa3942);
	HH4(d, a, b, c, x[8], s32, 0x8771f681);
	HH4(c, d, a, b, x[11], s33, 0x6d9d6122);
	HH4(b, c, d, a, x[14], s34, 0xfde5380c);
	HH4(a, b, c, d, x[1], s31, 0xa4beea44);
	HH4(d, a, b, c, x[4], s32, 0x4bdecfa9);
	HH4(c, d, a, b, x[7], s33, 0xf6bb4b60);
	HH4(b, c, d, a, x[10], s34, 0xbebfbc70);
	HH4(a, b, c, d, x[13], s31, 0x289b7ec6);
	HH4(d, a, b, c, x[0], s32, 0xeaa127fa);
	HH4(c, d, a, b, x[3], s33, 0xd4ef3085);
	HH4(b, c, d, a, x[6], s34, 0x4881d05);
	HH4(a, b, c, d, x[9], s31, 0xd9d4d039);
	HH4(d, a, b, c, x[12], s32, 0xe6db99e5);
	HH4(c, d, a, b, x[15], s33, 0x1fa27cf8);
	HH4(b, c, d, a, x[2], s34, 0xc4ac5665);

	/* Round 4 */
	II4(a, b, c, d, x[0], s41, 0xf4292244);
	II4(d, a, b, c, x[7], s42, 0x432aff97);
	II4(c, d, a, b, x[14], s43, 0xab9423a7);
	II4(b, c, d, a, x[5], s44, 0xfc93a039);
	II4(a, b, c, d, x[12], s41, 0x655b59c3);
	II4(d, a, b, c, x[3], s42, 0x8f0ccc92);
	II4(c, d, a, b, x[10], s43, 0xffeff47d);
	II4(b, c, d, a, x[1], s44, 0x85845dd1);
	II4(a, b, c, d, x[8], s41, 0x6fa87e4f);
	II4(d, a, b, c, x[15], s42, 0xfe2ce6e0);
	II4(c, d, a, b, x[6], s43, 0xa3014314);
	II4(b, c, d, a, x[13], s44, 0x4e0811a1);
	II4(a, b, c, d, x[4], s41, 0xf7537e82);
	II4(d, a, b, c, x[11], s42, 0xbd3af235);
	II4(c, d, a, b, x[2], s43, 0x2ad7d2bb);
	II4(b, c, d, a, x[9], s44, 0xeb86d391);
}
} // namespace

/**
 * StringProcess: 将单个输入字符串转换成MD5计算所需的消息数组
 * @param input 输入
 * @param[out] n_byte 用于给调用者传递额外的返回值，即最终Byte数组的长度
 * @return Byte消息数组
 */
Byte *StringProcess(string input, int *n_byte)
{
	// 将输入的字符串转换为Byte为单位的数组
	Byte *blocks = (Byte *)input.c_str();
	int length = input.length();

	// 计算原始消息长度（以比特为单位）
	int bitLength = length * 8;

	// paddingBits: 原始消息需要的padding长度（以bit为单位）
	// 对于给定的消息，将其补齐至length%512==448为止
	// 需要注意的是，即便给定的消息满足length%512==448，也需要再pad 512bits
	int paddingBits = bitLength % 512;
	if (paddingBits > 448)
	{
		paddingBits = 512 - (paddingBits - 448);
	}
	else if (paddingBits < 448)
	{
		paddingBits = 448 - paddingBits;
	}
	else if (paddingBits == 448)
	{
		paddingBits = 512;
	}

	// 原始消息需要的padding长度（以Byte为单位）
	int paddingBytes = paddingBits / 8;
	// 创建最终的字节数组
	// length + paddingBytes + 8:
	// 1. length为原始消息的长度（bits）
	// 2. paddingBytes为原始消息需要的padding长度（Bytes）
	// 3. 在pad到length%512==448之后，需要额外附加64bits的原始消息长度，即8个bytes
	int paddedLength = length + paddingBytes + 8;
	Byte *paddedMessage = new Byte[paddedLength];

	// 复制原始消息
	memcpy(paddedMessage, blocks, length);

	// 添加填充字节。填充时，第一位为1，后面的所有位均为0。
	// 所以第一个byte是0x80
	paddedMessage[length] = 0x80;							 // 添加一个0x80字节
	memset(paddedMessage + length + 1, 0, paddingBytes - 1); // 填充0字节

	// 添加消息长度（64比特，小端格式）
	for (int i = 0; i < 8; ++i)
	{
		// 特别注意此处应当将bitLength转换为uint64_t
		// 这里的length是原始消息的长度
		paddedMessage[length + paddingBytes + i] = ((uint64_t)length * 8 >> (i * 8)) & 0xFF;
	}

	// 验证长度是否满足要求。此时长度应当是512bit的倍数
	int residual = 8 * paddedLength % 512;
	// assert(residual == 0);

	// 在填充+添加长度之后，消息被分为n_blocks个512bit的部分
	*n_byte = paddedLength;
	return paddedMessage;
}


/**
 * MD5Hash: 将单个输入字符串转换成MD5
 * @param input 输入
 * @param[out] state 用于给调用者传递额外的返回值，即最终的缓冲区，也就是MD5的结果
 * @return Byte消息数组
 */
void MD5Hash(string input, bit32 *state)
{
	int messageLength = 0;
	Byte *paddedMessage = StringProcess(input, &messageLength);
	int n_blocks = messageLength / 64;

	// bit32* state= new bit32[4];
	state[0] = 0x67452301;
	state[1] = 0xefcdab89;
	state[2] = 0x98badcfe;
	state[3] = 0x10325476;

	// 逐block地更新state
	for (int i = 0; i < n_blocks; i += 1)
	{
		bit32 x[16];

		// 下面的处理，在理解上较为复杂
		for (int i1 = 0; i1 < 16; ++i1)
		{
			x[i1] = LoadWordLE(&paddedMessage[i * 64 + 4 * i1]);
		}

		bit32 a = state[0], b = state[1], c = state[2], d = state[3];

		MD5RoundsScalar(x, a, b, c, d);

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
	}

	// 下面的处理，在理解上较为复杂
	for (int i = 0; i < 4; i++)
	{
		state[i] = ByteSwap32(state[i]);
	}

	// 输出最终的hash结果
	// for (int i1 = 0; i1 < 4; i1 += 1)
	// {
	// 	cout << std::setw(8) << std::setfill('0') << hex << state[i1];
	// }
	// cout << endl;

	// 释放动态分配的内存
	// 实现SIMD并行算法的时候，也请记得及时回收内存！
	delete[] paddedMessage;
}

void MD5HashSIMD(const vector<string> &inputs, vector<bit32> &states)
{
	// 扁平输出：第i个口令的4个state写在states[i*4 ... i*4+3]
	states.assign(inputs.size() * 4, 0);
	if (inputs.empty())
	{
		return;
	}

	for (size_t base = 0; base < inputs.size(); base += 4)
	{
		// 固定4路分组，不足4路时只启用前laneCount个lane
		const int laneCount = static_cast<int>(min<size_t>(4, inputs.size() - base));
		Byte *paddedMessages[4] = {0, 0, 0, 0};
		int messageLengths[4] = {0, 0, 0, 0};
		int nBlocks[4] = {0, 0, 0, 0};

		for (int lane = 0; lane < laneCount; ++lane)
		{
			// 每个lane独立做padding，保留原StringProcess逻辑不变
			paddedMessages[lane] = StringProcess(inputs[base + lane], &messageLengths[lane]);
			nBlocks[lane] = messageLengths[lane] / 64;
		}

		bit32 laneState[4][4];
		for (int lane = 0; lane < 4; ++lane)
		{
			laneState[lane][0] = 0x67452301;
			laneState[lane][1] = 0xefcdab89;
			laneState[lane][2] = 0x98badcfe;
			laneState[lane][3] = 0x10325476;
		}

		int maxBlocks = 0;
		for (int lane = 0; lane < laneCount; ++lane)
		{
			maxBlocks = max(maxBlocks, nBlocks[lane]);
		}

		for (int block = 0; block < maxBlocks; ++block)
		{
			// 将4个口令当前block的第w个32-bit字打包成一个向量x[w]
			bit32x4 x[16];
			for (int w = 0; w < 16; ++w)
			{
				bit32 w0 = 0, w1 = 0, w2 = 0, w3 = 0;
				if (laneCount > 0 && block < nBlocks[0])
				{
					w0 = LoadWordLE(&paddedMessages[0][block * 64 + w * 4]);
				}
				if (laneCount > 1 && block < nBlocks[1])
				{
					w1 = LoadWordLE(&paddedMessages[1][block * 64 + w * 4]);
				}
				if (laneCount > 2 && block < nBlocks[2])
				{
					w2 = LoadWordLE(&paddedMessages[2][block * 64 + w * 4]);
				}
				if (laneCount > 3 && block < nBlocks[3])
				{
					w3 = LoadWordLE(&paddedMessages[3][block * 64 + w * 4]);
				}
				x[w] = MakeVec4(w0, w1, w2, w3);
			}

			// 读取4个lane当前state并执行一次SIMD轮函数
			bit32x4 a = MakeVec4(laneState[0][0], laneState[1][0], laneState[2][0], laneState[3][0]);
			bit32x4 b = MakeVec4(laneState[0][1], laneState[1][1], laneState[2][1], laneState[3][1]);
			bit32x4 c = MakeVec4(laneState[0][2], laneState[1][2], laneState[2][2], laneState[3][2]);
			bit32x4 d = MakeVec4(laneState[0][3], laneState[1][3], laneState[2][3], laneState[3][3]);

			MD5RoundsSIMD(x, a, b, c, d);

			// 写回时只更新仍有该block的lane，避免短消息lane被污染
			bit32 aOut[4], bOut[4], cOut[4], dOut[4];
			StoreVec4(a, aOut);
			StoreVec4(b, bOut);
			StoreVec4(c, cOut);
			StoreVec4(d, dOut);

			for (int lane = 0; lane < laneCount; ++lane)
			{
				if (block < nBlocks[lane])
				{
					laneState[lane][0] += aOut[lane];
					laneState[lane][1] += bOut[lane];
					laneState[lane][2] += cOut[lane];
					laneState[lane][3] += dOut[lane];
				}
			}
		}

		for (int lane = 0; lane < laneCount; ++lane)
		{
			// 与标量路径一致，最终输出前进行字节序转换
			for (int i = 0; i < 4; ++i)
			{
				states[(base + lane) * 4 + i] = ByteSwap32(laneState[lane][i]);
			}
			delete[] paddedMessages[lane];
		}
	}
}

