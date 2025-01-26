// Generates DXBC bytecode for use with D3DDevice.CreateInputLayout. So that you don't need to keep an actual shader around.
// 
// CreateInputLayout does actually validate everything about the shader, not just the input layour part.
// So this function generates output that's byte-for-byte identical with D3DCompile.
//
// The bytecode format and checksum algorithm was reverse engineered with the help of Wine source.

#include <d3d11.h> // only for D3D11_INPUT_ELEMENT_DESC
#include <string.h> // memcpy, memset, strcmp, strlen

void md5_transform(unsigned scratch[4], const unsigned input[16])
{
	unsigned a = scratch[0];
	unsigned b = scratch[1];
	unsigned c = scratch[2];
	unsigned d = scratch[3];

	#define ROTATE_LEFT(x, n) ((x << n) | (x >> (32 - n)))
	#define MD5_F(x, y, z) ((x & y) | (~x & z))
	#define MD5_G(x, y, z) ((x & z) | (y & ~z))
	#define MD5_H(x, y, z) (x ^ y ^ z)
	#define MD5_I(x, y, z) (y ^ (x | ~z))
	#define MD5_FF(a, b, c, d, x, s, ac) a += MD5_F(b, c, d) + x + ac; a = ROTATE_LEFT(a, s) + b;
	#define MD5_GG(a, b, c, d, x, s, ac) a += MD5_G(b, c, d) + x + ac; a = ROTATE_LEFT(a, s) + b;
	#define MD5_HH(a, b, c, d, x, s, ac) a += MD5_H(b, c, d) + x + ac; a = ROTATE_LEFT(a, s) + b;
	#define MD5_II(a, b, c, d, x, s, ac) a += MD5_I(b, c, d) + x + ac; a = ROTATE_LEFT(a, s) + b;
	MD5_FF(a, b, c, d, input[0], 7, 3614090360);
	MD5_FF(d, a, b, c, input[1], 12, 3905402710);
	MD5_FF(c, d, a, b, input[2], 17, 606105819);
	MD5_FF(b, c, d, a, input[3], 22, 3250441966);
	MD5_FF(a, b, c, d, input[4], 7, 4118548399);
	MD5_FF(d, a, b, c, input[5], 12, 1200080426);
	MD5_FF(c, d, a, b, input[6], 17, 2821735955);
	MD5_FF(b, c, d, a, input[7], 22, 4249261313);
	MD5_FF(a, b, c, d, input[8], 7, 1770035416);
	MD5_FF(d, a, b, c, input[9], 12, 2336552879);
	MD5_FF(c, d, a, b, input[10], 17, 4294925233);
	MD5_FF(b, c, d, a, input[11], 22, 2304563134);
	MD5_FF(a, b, c, d, input[12], 7, 1804603682);
	MD5_FF(d, a, b, c, input[13], 12, 4254626195);
	MD5_FF(c, d, a, b, input[14], 17, 2792965006);
	MD5_FF(b, c, d, a, input[15], 22, 1236535329);
	MD5_GG(a, b, c, d, input[1], 5, 4129170786);
	MD5_GG(d, a, b, c, input[6], 9, 3225465664);
	MD5_GG(c, d, a, b, input[11], 14, 643717713);
	MD5_GG(b, c, d, a, input[0], 20, 3921069994);
	MD5_GG(a, b, c, d, input[5], 5, 3593408605);
	MD5_GG(d, a, b, c, input[10], 9, 38016083);
	MD5_GG(c, d, a, b, input[15], 14, 3634488961);
	MD5_GG(b, c, d, a, input[4], 20, 3889429448);
	MD5_GG(a, b, c, d, input[9], 5, 568446438);
	MD5_GG(d, a, b, c, input[14], 9, 3275163606);
	MD5_GG(c, d, a, b, input[3], 14, 4107603335);
	MD5_GG(b, c, d, a, input[8], 20, 1163531501);
	MD5_GG(a, b, c, d, input[13], 5, 2850285829);
	MD5_GG(d, a, b, c, input[2], 9, 4243563512);
	MD5_GG(c, d, a, b, input[7], 14, 1735328473);
	MD5_GG(b, c, d, a, input[12], 20, 2368359562);
	MD5_HH(a, b, c, d, input[5], 4, 4294588738);
	MD5_HH(d, a, b, c, input[8], 11, 2272392833);
	MD5_HH(c, d, a, b, input[11], 16, 1839030562);
	MD5_HH(b, c, d, a, input[14], 23, 4259657740);
	MD5_HH(a, b, c, d, input[1], 4, 2763975236);
	MD5_HH(d, a, b, c, input[4], 11, 1272893353);
	MD5_HH(c, d, a, b, input[7], 16, 4139469664);
	MD5_HH(b, c, d, a, input[10], 23, 3200236656);
	MD5_HH(a, b, c, d, input[13], 4, 681279174);
	MD5_HH(d, a, b, c, input[0], 11, 3936430074);
	MD5_HH(c, d, a, b, input[3], 16, 3572445317);
	MD5_HH(b, c, d, a, input[6], 23, 76029189);
	MD5_HH(a, b, c, d, input[9], 4, 3654602809);
	MD5_HH(d, a, b, c, input[12], 11, 3873151461);
	MD5_HH(c, d, a, b, input[15], 16, 530742520);
	MD5_HH(b, c, d, a, input[2], 23, 3299628645);
	MD5_II(a, b, c, d, input[0], 6, 4096336452);
	MD5_II(d, a, b, c, input[7], 10, 1126891415);
	MD5_II(c, d, a, b, input[14], 15, 2878612391);
	MD5_II(b, c, d, a, input[5], 21, 4237533241);
	MD5_II(a, b, c, d, input[12], 6, 1700485571);
	MD5_II(d, a, b, c, input[3], 10, 2399980690);
	MD5_II(c, d, a, b, input[10], 15, 4293915773);
	MD5_II(b, c, d, a, input[1], 21, 2240044497);
	MD5_II(a, b, c, d, input[8], 6, 1873313359);
	MD5_II(d, a, b, c, input[15], 10, 4264355552);
	MD5_II(c, d, a, b, input[6], 15, 2734768916);
	MD5_II(b, c, d, a, input[13], 21, 1309151649);
	MD5_II(a, b, c, d, input[4], 6, 4149444226);
	MD5_II(d, a, b, c, input[11], 10, 3174756917);
	MD5_II(c, d, a, b, input[2], 15, 718787259);
	MD5_II(b, c, d, a, input[9], 21, 3951481745);
	#undef ROTATE_LEFT
	#undef MD5_F
	#undef MD5_G
	#undef MD5_H
	#undef MD5_I
	#undef MD5_FF
	#undef MD5_GG
	#undef MD5_HH
	#undef MD5_II

	scratch[0] += a;
	scratch[1] += b;
	scratch[2] += c;
	scratch[3] += d;
}
int generate_bytecode_for_input_layout(unsigned char out[1024], const D3D11_INPUT_ELEMENT_DESC inputs[], int num_inputs)
{
	struct
	{
		char magic[4];
		char md5[16];
		short major_version;
		short minor_version;
		int file_size;
		int number_of_chunks;
		int rdef_chunk_offset;
		int isgn_chunk_offset;
		int osgn_chunk_offset;
		int shdr_chunk_offset;
		int stat_chunk_offset;
		char rdef_chunk_header[4];
		int rdef_chunk_size;
		int number_of_constant_buffers;
		int offset_of_constant_buffers;
		int number_of_resource_bindings;
		int offset_of_resource_bindings;
		char rdef_minor_version;
		char rdef_major_version;
		short shader_type;
		int compile_flags;
		int offset_of_compiler_string;
		char compiler_string[40];
		char isgn_chunk_header[4];
		int isgn_chunk_size;
		int number_of_elements;
		int always_8;
	}
	header = 
	{
		.magic = { 'D', 'X', 'B', 'C' },
		.md5 = { 0 }, // filled in later
		.major_version = 1,
		.minor_version = 0,
		.file_size = 0, // filled in later
		.number_of_chunks = 5,
		.rdef_chunk_offset = 52,
		.isgn_chunk_offset = 128,
		.osgn_chunk_offset = 0, // filled in later
		.shdr_chunk_offset = 0, // filled in later
		.stat_chunk_offset = 0, // filled in later
		.rdef_chunk_header = { 'R', 'D', 'E', 'F' },
		.rdef_chunk_size = 68,
		.number_of_constant_buffers = 0,
		.offset_of_constant_buffers = 0,
		.number_of_resource_bindings = 0,
		.offset_of_resource_bindings = 28,
		.rdef_major_version = 4,
		.rdef_minor_version = 0,
		.shader_type = 0xFFFE, // vertex shader
		.compile_flags = 0x104, // SKIP_OPTIMIZATION | NO_PRESHADER
		.offset_of_compiler_string = 28,
		.compiler_string = "Microsoft (R) HLSL Shader Compiler 10.1",
		.isgn_chunk_header = { 'I', 'S', 'G', 'N' },
		.isgn_chunk_size = 0, // filled in later
		.number_of_elements = num_inputs,
		.always_8 = 8, // I have no idea what this is
	};

	static const unsigned char FOOTER[160] =
	{
		'O', 'S', 'G', 'N', // OSGN chunk header
		8, 0, 0, 0, // OSGN chunk size
		0, 0, 0, 0, // number of elements
		8, 0, 0, 0, // I don't know what this is but it's always 8
		'S', 'H', 'D', 'R', // SHDR chunk header
		12, 0, 0, 0, // SHDR chunk size
		0x40, // major and minor version
		0, // I don't know what this is
		1, 0, // program type: 1 = vertex shader
		3, 0, 0, 0, // number of instructions?
		0x3E, 0x00, 0x00, 0x01, // probably just a ret instruction
		'S', 'T', 'A', 'T', // STAT chunk header
		116, 0, 0, 0, // STAT chunk size
		1, 0, 0, 0, // number of instruction and various other statistics that don't matter
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	};

	// per-input element data
	struct
	{
		int semantic_offset; // offset from ISGN chunk data where semantic name is
		int semantic_index;
		int semantic_value_type; // always 0 for inputs
		int type; // uint = 1, int = 2, float = 3
		int register_index; // 0, 1, 2, 3... per input
		unsigned char components; // X|Y|Z|W 4 bit mask float4 = 0b1111, float3 = 0b0111
		unsigned char components_used; // what is actually used - always 0 for us
		short padding;
	} element;

	// semantic name block goes after all the elements
	char* start_of_elements = (char*)out + sizeof header;
	char* start_of_isgn_data = start_of_elements - 8;
	char* semantics0 = start_of_elements + num_inputs * sizeof element;
	char* semantics1 = semantics0;

	// append all input elements
	for (int i = 0; i < num_inputs; i++)
	{
		memset(&element, 0, sizeof element);
		element.semantic_index = inputs[i].SemanticIndex;
		element.register_index = i;

		// either reuse an existing semantic name or append a new one
		for (char* semantic = semantics0;;)
		{
			if (semantic == semantics1 || strcmp(inputs[i].SemanticName, semantic) == 0)
			{
				element.semantic_offset = (int)(semantic - start_of_isgn_data);
				if (semantic == semantics1)
				{
					strcpy(semantic, inputs[i].SemanticName);
					semantics1 += strlen(semantic) + 1;
				}
				break;
			}
			semantic += strlen(semantic) + 1;
		}

		// translate the format into a type and component mask
		enum
		{
			UINT = 1,
			INT = 2,
			FLOAT = 3,
			X = 0b0001,
			XY = 0b0011,
			XYZ = 0b0111,
			XYZW = 0b1111,
		};
		switch (inputs[i].Format)
		{
			case DXGI_FORMAT_R32G32B32A32_FLOAT: element.type = FLOAT; element.components = XYZW; break;
			case DXGI_FORMAT_R32G32B32A32_SINT: element.type = INT; element.components = XYZW; break;
			case DXGI_FORMAT_R32G32B32A32_UINT: element.type = UINT; element.components = XYZW; break;
			case DXGI_FORMAT_R32G32B32_FLOAT: element.type = FLOAT; element.components = XYZ; break;
			case DXGI_FORMAT_R32G32B32_SINT: element.type = INT; element.components = XYZ; break;
			case DXGI_FORMAT_R32G32B32_UINT: element.type = UINT; element.components = XYZ; break;
			case DXGI_FORMAT_R32G32_FLOAT: element.type = FLOAT; element.components = XY; break;
			case DXGI_FORMAT_R32G32_SINT: element.type = INT; element.components = XY; break;
			case DXGI_FORMAT_R32G32_UINT: element.type = UINT; element.components = XY; break;
			case DXGI_FORMAT_R32_FLOAT: element.type = FLOAT; element.components = X; break;
			case DXGI_FORMAT_R32_SINT: element.type = INT; element.components = X; break;
			case DXGI_FORMAT_R32_UINT: element.type = UINT; element.components = X; break;
			case DXGI_FORMAT_R16G16B16A16_FLOAT: element.type = FLOAT; element.components = XYZW; break;
			case DXGI_FORMAT_R16G16B16A16_SINT: element.type = INT; element.components = XYZW; break;
			case DXGI_FORMAT_R16G16B16A16_UINT: element.type = UINT; element.components = XYZW; break;
			case DXGI_FORMAT_R16G16B16A16_SNORM: element.type = FLOAT; element.components = XYZW; break;
			case DXGI_FORMAT_R16G16B16A16_UNORM: element.type = FLOAT; element.components = XYZW; break;
			case DXGI_FORMAT_R16G16_FLOAT: element.type = FLOAT; element.components = XY; break;
			case DXGI_FORMAT_R16G16_SINT: element.type = INT; element.components = XY; break;
			case DXGI_FORMAT_R16G16_UINT: element.type = UINT; element.components = XY; break;
			case DXGI_FORMAT_R16G16_SNORM: element.type = FLOAT; element.components = XY; break;
			case DXGI_FORMAT_R16G16_UNORM: element.type = FLOAT; element.components = XY; break;
			case DXGI_FORMAT_R16_FLOAT: element.type = FLOAT; element.components = X; break;
			case DXGI_FORMAT_R16_SINT: element.type = INT; element.components = X; break;
			case DXGI_FORMAT_R16_UINT: element.type = UINT; element.components = X; break;
			case DXGI_FORMAT_R16_SNORM: element.type = FLOAT; element.components = X; break;
			case DXGI_FORMAT_R8G8B8A8_SINT: element.type = INT; element.components = XYZW; break;
			case DXGI_FORMAT_R8G8B8A8_UINT: element.type = UINT; element.components = XYZW; break;
			case DXGI_FORMAT_R8G8B8A8_SNORM: element.type = FLOAT; element.components = XYZW; break;
			case DXGI_FORMAT_R8G8B8A8_UNORM: element.type = FLOAT; element.components = XYZW; break;
			case DXGI_FORMAT_R8G8_SINT: element.type = INT; element.components = XY; break;
			case DXGI_FORMAT_R8G8_UINT: element.type = UINT; element.components = XY; break;
			case DXGI_FORMAT_R8G8_SNORM: element.type = FLOAT; element.components = XY; break;
			case DXGI_FORMAT_R8G8_UNORM: element.type = FLOAT; element.components = XY; break;
			case DXGI_FORMAT_R8_SINT: element.type = INT; element.components = X; break;
			case DXGI_FORMAT_R8_UINT: element.type = UINT; element.components = X; break;
			case DXGI_FORMAT_R8_SNORM: element.type = FLOAT; element.components = X; break;
			case DXGI_FORMAT_R8_UNORM: element.type = FLOAT; element.components = X; break;
			case DXGI_FORMAT_R10G10B10A2_UNORM: element.type = FLOAT; element.components = XYZ; break;
			case DXGI_FORMAT_R10G10B10A2_UINT: element.type = UINT; element.components = XYZ; break;
			case DXGI_FORMAT_R11G11B10_FLOAT: element.type = FLOAT; element.components = XYZ; break;
			case DXGI_FORMAT_B5G6R5_UNORM: element.type = FLOAT; element.components = XYZ; break;
			case DXGI_FORMAT_B5G5R5A1_UNORM: element.type = FLOAT; element.components = XYZW; break;
			case DXGI_FORMAT_B8G8R8X8_UNORM: element.type = FLOAT; element.components = XYZ; break;
		}

		// copy to correct place
		memcpy(start_of_elements + i * sizeof element, &element, sizeof element);
	}

	// semantics strings are padded with 0xAB until 4 byte aligned
	while ((semantics1 - semantics0) % 4)
		*semantics1++ = '\xAB';

	// fill out the rest of the header
	char* end_of_isgn_data = semantics1;
	header.file_size = sizeof header + (int)(end_of_isgn_data - start_of_elements) + sizeof FOOTER;
	header.osgn_chunk_offset = (int)(end_of_isgn_data - (char*)out);
	header.shdr_chunk_offset = header.osgn_chunk_offset + 16;
	header.stat_chunk_offset = header.shdr_chunk_offset + 20;
	header.isgn_chunk_size = (int)(end_of_isgn_data - start_of_isgn_data);

	// sandwich between header and footer
	memcpy(out, &header, sizeof header);
	memcpy(end_of_isgn_data, FOOTER, sizeof FOOTER);
	
	// calculate modified MD5 checksum
	unsigned md5[4] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476 };
	union { unsigned u32[16]; unsigned char u8[64]; } input = { 0 };

	// process whole chunks
	unsigned char* data = out + 20;
	int size = header.file_size - 20;
	int size_truncated_to_whole_chunk = size & ~63;
	for (int i = 0; i < size_truncated_to_whole_chunk; i += 64)
	{
		memcpy(input.u8, data + i, sizeof input);
		md5_transform(md5, input.u32);
	}

	// process leftover chunks
	unsigned char* last_chunk_data = data + size_truncated_to_whole_chunk;
	int last_chunk_size = size - size_truncated_to_whole_chunk;
	if (last_chunk_size >= 56)
	{
		memset(input.u8, 0, sizeof input);
		memcpy(input.u8, last_chunk_data, last_chunk_size);
		input.u8[last_chunk_size] = 0x80;
		md5_transform(md5, input.u32);
	}
	memset(input.u8, 0, sizeof input);
	input.u32[0] = (size * 8);
	input.u32[15] = (size * 2) | 1;
	if (last_chunk_size < 56)
	{
		memcpy(input.u8 + 4, last_chunk_data, last_chunk_size);
		input.u8[4 + last_chunk_size] = 0x80;
	}
	md5_transform(md5, input.u32);

	// copy checksum to header
	memcpy(out + 4, md5, 16);
	return header.file_size;
}

// Testing

#include <d3dcompiler.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#pragma comment(lib, "d3dcompiler.lib")

int main(void)
{
	unsigned long long total_qpc_genbytecode = 0;
	unsigned long long total_qpc_d3dcompile = 0;

	static const DXGI_FORMAT FORMATS[] =
	{
		DXGI_FORMAT_R32G32B32A32_FLOAT,
		DXGI_FORMAT_R32G32B32A32_SINT,
		DXGI_FORMAT_R32G32B32A32_UINT,
		DXGI_FORMAT_R32G32B32_FLOAT,
		DXGI_FORMAT_R32G32B32_SINT,
		DXGI_FORMAT_R32G32B32_UINT,
		DXGI_FORMAT_R32G32_FLOAT,
		DXGI_FORMAT_R32G32_SINT,
		DXGI_FORMAT_R32G32_UINT,
		DXGI_FORMAT_R32_FLOAT,
		DXGI_FORMAT_R32_SINT,
		DXGI_FORMAT_R32_UINT,
	};
	D3D11_INPUT_ELEMENT_DESC inputs[6] = { 0 }; // this is exponentially slow with higher max inputs
	for (int num_inputs = 0; num_inputs < _countof(inputs); num_inputs++)
	{
		LARGE_INTEGER qpf;
		QueryPerformanceFrequency(&qpf);

		double total_seconds_genbytecode = total_qpc_genbytecode / (double)qpf.QuadPart;
		double total_seconds_d3dcompile = total_qpc_d3dcompile / (double)qpf.QuadPart;
		double ratio = total_seconds_d3dcompile / total_seconds_genbytecode;
		printf("%d inputs... genbytecode: %.3f, d3dcompile: %.3f (%.1fx faster)\n", num_inputs,
			total_seconds_genbytecode, total_seconds_d3dcompile, ratio);

		int num_permutations = 1; // pow(countof(FORMATS), num_inputs)
		for (int i = 0; i < num_inputs; i++)
			num_permutations *= _countof(FORMATS);
		
		for (int permutation = 0; permutation < num_permutations; permutation++)
		{
			int state = permutation;
			for (int i = 0; i < num_inputs; i++)
			{
				DXGI_FORMAT format = FORMATS[state % _countof(FORMATS)];
				state /= _countof(FORMATS);
				inputs[i].SemanticName = "X"; // can be any sensible semantic name + index
				inputs[i].SemanticIndex = i;
				inputs[i].Format = format;
			}

			LARGE_INTEGER t0, t1;
			QueryPerformanceCounter(&t0);
			
			unsigned char our_bytecode[1024];
			int size_of_our_bytecode = generate_bytecode_for_input_layout(our_bytecode, inputs, num_inputs);
			
			QueryPerformanceCounter(&t1);
			total_qpc_genbytecode += t1.QuadPart - t0.QuadPart;

			char shader[1024] = "";
			char* cursor = shader;
			cursor += sprintf(cursor, "struct Input { ");
			for (int i = 0; i < num_inputs; i++)
			{
				const char* type = NULL;
				switch (inputs[i].Format)
				{
					case DXGI_FORMAT_R32G32B32A32_FLOAT: type = "float4"; break;
					case DXGI_FORMAT_R32G32B32A32_SINT: type = "int4"; break;
					case DXGI_FORMAT_R32G32B32A32_UINT: type = "uint4"; break;
					case DXGI_FORMAT_R32G32B32_FLOAT: type = "float3"; break;
					case DXGI_FORMAT_R32G32B32_SINT: type = "int3"; break;
					case DXGI_FORMAT_R32G32B32_UINT: type = "uint3"; break;
					case DXGI_FORMAT_R32G32_FLOAT: type = "float2"; break;
					case DXGI_FORMAT_R32G32_SINT: type = "int2"; break;
					case DXGI_FORMAT_R32G32_UINT: type = "uint2"; break;
					case DXGI_FORMAT_R32_FLOAT: type = "float"; break;
					case DXGI_FORMAT_R32_SINT: type = "int"; break;
					case DXGI_FORMAT_R32_UINT: type = "uint"; break;
				}
				cursor += sprintf(cursor, "%s v%d: X%d;", type, i, i);
			}
			cursor += sprintf(cursor, " }; void vertex_shader(Input input) {}");

			QueryPerformanceCounter(&t0);
			
			ID3DBlob* blob;
			assert(SUCCEEDED(D3DCompile(shader, strlen(shader), NULL, NULL, NULL, "vertex_shader", "vs_4_0", D3DCOMPILE_SKIP_OPTIMIZATION, 0, &blob, NULL)));
			
			QueryPerformanceCounter(&t1);
			total_qpc_d3dcompile += t1.QuadPart - t0.QuadPart;

			unsigned char* d3d_bytecode = blob->lpVtbl->GetBufferPointer(blob);
			int size_of_d3d_bytecode = (int)blob->lpVtbl->GetBufferSize(blob);

			#if 0
			{
				FILE* f;
				f = fopen("ours.dxbc", "wb");
				fwrite(our_bytecode, size_of_our_bytecode, 1, f);
				fclose(f);
				f = fopen("d3ds.dxbc", "wb");
				fwrite(d3d_bytecode, size_of_d3d_bytecode, 1, f);
				fclose(f);
			}
			#endif

			assert(size_of_our_bytecode == size_of_d3d_bytecode);
			assert(memcmp(our_bytecode, d3d_bytecode, size_of_d3d_bytecode) == 0);
			
			blob->lpVtbl->Release(blob);
		}
	}
}
