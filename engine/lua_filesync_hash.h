#ifndef LUA_FILESYNC_HASH_H
#define LUA_FILESYNC_HASH_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline uint32_t LuaFileHashRotate( uint32_t value, unsigned count )
{
	return (value >> count) | (value << (32 - count));
}

static void LuaFileHashBlock( uint32_t state[8], const unsigned char *data )
{
	static const uint32_t constants[64] = {
		0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
		0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
		0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
		0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
		0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
		0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
		0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
		0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
	};
	uint32_t words[64];
	for ( int i = 0; i < 16; ++i )
		words[i] = (uint32_t(data[i*4]) << 24) | (uint32_t(data[i*4+1]) << 16) | (uint32_t(data[i*4+2]) << 8) | data[i*4+3];
	for ( int i = 16; i < 64; ++i )
	{
		const uint32_t x = words[i-15], y = words[i-2];
		const uint32_t a = LuaFileHashRotate(x,7) ^ LuaFileHashRotate(x,18) ^ (x >> 3);
		const uint32_t b = LuaFileHashRotate(y,17) ^ LuaFileHashRotate(y,19) ^ (y >> 10);
		words[i] = words[i-16] + a + words[i-7] + b;
	}
	uint32_t a=state[0], b=state[1], c=state[2], d=state[3];
	uint32_t e=state[4], f=state[5], g=state[6], h=state[7];
	for ( int i = 0; i < 64; ++i )
	{
		const uint32_t s1 = LuaFileHashRotate(e,6) ^ LuaFileHashRotate(e,11) ^ LuaFileHashRotate(e,25);
		const uint32_t s0 = LuaFileHashRotate(a,2) ^ LuaFileHashRotate(a,13) ^ LuaFileHashRotate(a,22);
		const uint32_t t1 = h + s1 + ((e & f) ^ (~e & g)) + constants[i] + words[i];
		const uint32_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
		h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
	}
	state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
	state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void LuaFileHash( const void *source, size_t length, unsigned char digest[32] )
{
	uint32_t state[8] = { 0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
		0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u };
	const unsigned char *data = static_cast<const unsigned char *>(source);
	const size_t original = length;
	while ( length >= 64 ) { LuaFileHashBlock(state, data); data += 64; length -= 64; }
	unsigned char tail[128] = { 0 };
	if ( length ) memcpy( tail, data, length );
	tail[length] = 0x80;
	const size_t padded = length < 56 ? 64 : 128;
	const uint64_t bits = uint64_t(original) * 8;
	for ( int i=0; i<8; ++i ) tail[padded-1-i] = static_cast<unsigned char>(bits >> (i*8));
	LuaFileHashBlock(state, tail);
	if ( padded == 128 ) LuaFileHashBlock(state, tail+64);
	for ( int i=0; i<8; ++i )
		for ( int j=0; j<4; ++j ) digest[i*4+j] = static_cast<unsigned char>(state[i] >> (24-j*8));
}

#endif
