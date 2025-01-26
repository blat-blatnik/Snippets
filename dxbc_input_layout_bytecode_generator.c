// Generates DXBC bytecode for use with D3DDevice.CreateInputLayout. So that you don't need to keep an actual shader around.
// 
// CreateInputLayout does actually validate everything about the shader, not just the input layour part.
// So this function generates output that's byte-for-byte identical with D3DCompile.
//
// The bytecode format and checksum algorithm was reverse engineered with the help of Wine source.

#include <d3d11.h> // only for D3D11_INPUT_ELEMENT_DESC
#include <string.h> // memcpy, memset, strcmp, strlen

int generate_bytecode_for_input_layout(unsigned char output[1024], const D3D11_INPUT_ELEMENT_DESC inputs[], int num_inputs)
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
	char* start_of_elements = (char*)output + sizeof header;
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
	header.osgn_chunk_offset = (int)(end_of_isgn_data - (char*)output);
	header.shdr_chunk_offset = header.osgn_chunk_offset + 16;
	header.stat_chunk_offset = header.shdr_chunk_offset + 20;
	header.isgn_chunk_size = (int)(end_of_isgn_data - start_of_isgn_data);

	// sandwich between header and footer
	memcpy(output, &header, sizeof header);
	memcpy(end_of_isgn_data, FOOTER, sizeof FOOTER);
	
	// calculate modified MD5 checksum
	unsigned md5[4] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476 };
	unsigned char* data = output + 20;
	int data_size = header.file_size - 20;
	for (int size = data_size; size >= -8; size -= 64, data += 64)
	{
		union { unsigned u32[16]; unsigned char u8[64]; } input = { 0 };
		if (size >= 64)
			memcpy(input.u8, data, 64);
		else if (size >= 56)
			input.u8[size] = 0x80; // end of footer is all 0 so don't need to copy anything
		else
		{
			input.u32[0] = (data_size * 8);
			input.u32[15] = (data_size * 2) | 1;
			if (size >= 0)
				input.u8[4 + size] = 0x80;
		}

		unsigned a = md5[0];
		unsigned b = md5[1];
		unsigned c = md5[2];
		unsigned d = md5[3];
		#define MD5_F1(x, y, z) ((x & y) | (~x & z))
		#define MD5_F2(x, y, z) ((x & z) | (y & ~z))
		#define MD5_F3(x, y, z) (x ^ y ^ z)
		#define MD5_F4(x, y, z) (y ^ (x | ~z))
		#define MD5_STEP(f, w, x, y, z, index, a, s) (w += f(x, y, z) + input.u32[index] + a, w = w << s | w >> (32 - s), w += x)
		MD5_STEP(MD5_F1, a, b, c, d, 0, 0xD76AA478, 7);
		MD5_STEP(MD5_F1, d, a, b, c, 1, 0xE8C7B756, 12);
		MD5_STEP(MD5_F1, c, d, a, b, 2, 0x242070DB, 17);
		MD5_STEP(MD5_F1, b, c, d, a, 3, 0xC1BDCEEE, 22);
		MD5_STEP(MD5_F1, a, b, c, d, 4, 0xF57C0FAF, 7);
		MD5_STEP(MD5_F1, d, a, b, c, 5, 0x4787C62A, 12);
		MD5_STEP(MD5_F1, c, d, a, b, 6, 0xA8304613, 17);
		MD5_STEP(MD5_F1, b, c, d, a, 7, 0xFD469501, 22);
		MD5_STEP(MD5_F1, a, b, c, d, 8, 0x698098D8, 7);
		MD5_STEP(MD5_F1, d, a, b, c, 9, 0x8B44F7AF, 12);
		MD5_STEP(MD5_F1, c, d, a, b, 10, 0xFFFF5BB1, 17);
		MD5_STEP(MD5_F1, b, c, d, a, 11, 0x895CD7BE, 22);
		MD5_STEP(MD5_F1, a, b, c, d, 12, 0x6B901122, 7);
		MD5_STEP(MD5_F1, d, a, b, c, 13, 0xFD987193, 12);
		MD5_STEP(MD5_F1, c, d, a, b, 14, 0xA679438E, 17);
		MD5_STEP(MD5_F1, b, c, d, a, 15, 0x49B40821, 22);
		MD5_STEP(MD5_F2, a, b, c, d, 1, 0xF61E2562, 5);
		MD5_STEP(MD5_F2, d, a, b, c, 6, 0xC040B340, 9);
		MD5_STEP(MD5_F2, c, d, a, b, 11, 0x265E5A51, 14);
		MD5_STEP(MD5_F2, b, c, d, a, 0, 0xE9B6C7AA, 20);
		MD5_STEP(MD5_F2, a, b, c, d, 5, 0xD62F105D, 5);
		MD5_STEP(MD5_F2, d, a, b, c, 10, 0x02441453, 9);
		MD5_STEP(MD5_F2, c, d, a, b, 15, 0xD8A1E681, 14);
		MD5_STEP(MD5_F2, b, c, d, a, 4, 0xE7D3FBC8, 20);
		MD5_STEP(MD5_F2, a, b, c, d, 9, 0x21E1CDE6, 5);
		MD5_STEP(MD5_F2, d, a, b, c, 14, 0xC33707D6, 9);
		MD5_STEP(MD5_F2, c, d, a, b, 3, 0xF4D50D87, 14);
		MD5_STEP(MD5_F2, b, c, d, a, 8, 0x455A14ED, 20);
		MD5_STEP(MD5_F2, a, b, c, d, 13, 0xA9E3E905, 5);
		MD5_STEP(MD5_F2, d, a, b, c, 2, 0xFCEFA3F8, 9);
		MD5_STEP(MD5_F2, c, d, a, b, 7, 0x676F02D9, 14);
		MD5_STEP(MD5_F2, b, c, d, a, 12, 0x8D2A4C8A, 20);
		MD5_STEP(MD5_F3, a, b, c, d, 5, 0xFFFA3942, 4);
		MD5_STEP(MD5_F3, d, a, b, c, 8, 0x8771F681, 11);
		MD5_STEP(MD5_F3, c, d, a, b, 11, 0x6D9D6122, 16);
		MD5_STEP(MD5_F3, b, c, d, a, 14, 0xFDE5380C, 23);
		MD5_STEP(MD5_F3, a, b, c, d, 1, 0xA4BEEA44, 4);
		MD5_STEP(MD5_F3, d, a, b, c, 4, 0x4BDECFA9, 11);
		MD5_STEP(MD5_F3, c, d, a, b, 7, 0xF6BB4B60, 16);
		MD5_STEP(MD5_F3, b, c, d, a, 10, 0xBEBFBC70, 23);
		MD5_STEP(MD5_F3, a, b, c, d, 13, 0x289B7EC6, 4);
		MD5_STEP(MD5_F3, d, a, b, c, 0, 0xEAA127FA, 11);
		MD5_STEP(MD5_F3, c, d, a, b, 3, 0xD4EF3085, 16);
		MD5_STEP(MD5_F3, b, c, d, a, 6, 0x04881D05, 23);
		MD5_STEP(MD5_F3, a, b, c, d, 9, 0xD9D4D039, 4);
		MD5_STEP(MD5_F3, d, a, b, c, 12, 0xE6DB99E5, 11);
		MD5_STEP(MD5_F3, c, d, a, b, 15, 0x1FA27CF8, 16);
		MD5_STEP(MD5_F3, b, c, d, a, 2, 0xC4AC5665, 23);
		MD5_STEP(MD5_F4, a, b, c, d, 0, 0xF4292244, 6);
		MD5_STEP(MD5_F4, d, a, b, c, 7, 0x432AFF97, 10);
		MD5_STEP(MD5_F4, c, d, a, b, 14, 0xAB9423A7, 15);
		MD5_STEP(MD5_F4, b, c, d, a, 5, 0xFC93A039, 21);
		MD5_STEP(MD5_F4, a, b, c, d, 12, 0x655B59C3, 6);
		MD5_STEP(MD5_F4, d, a, b, c, 3, 0x8F0CCC92, 10);
		MD5_STEP(MD5_F4, c, d, a, b, 10, 0xFFEFF47D, 15);
		MD5_STEP(MD5_F4, b, c, d, a, 1, 0x85845DD1, 21);
		MD5_STEP(MD5_F4, a, b, c, d, 8, 0x6FA87E4F, 6);
		MD5_STEP(MD5_F4, d, a, b, c, 15, 0xFE2CE6E0, 10);
		MD5_STEP(MD5_F4, c, d, a, b, 6, 0xA3014314, 15);
		MD5_STEP(MD5_F4, b, c, d, a, 13, 0x4E0811A1, 21);
		MD5_STEP(MD5_F4, a, b, c, d, 4, 0xF7537E82, 6);
		MD5_STEP(MD5_F4, d, a, b, c, 11, 0xBD3AF235, 10);
		MD5_STEP(MD5_F4, c, d, a, b, 2, 0x2AD7D2BB, 15);
		MD5_STEP(MD5_F4, b, c, d, a, 9, 0xEB86D391, 21);
		#undef MD5_F1
		#undef MD5_F2
		#undef MD5_F3
		#undef MD5_F4
		#undef MD5_STEP
		md5[0] += a;
		md5[1] += b;
		md5[2] += c;
		md5[3] += d;
	}

	// copy checksum to header
	memcpy(output + 4, md5, 16);
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
	double total_calls = 0;

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
	D3D11_INPUT_ELEMENT_DESC inputs[16] = { 0 }; // D3D supports max 16 input slots
	for (int num_inputs = 0; num_inputs < _countof(inputs); num_inputs++)
	{
		LARGE_INTEGER qpf;
		QueryPerformanceFrequency(&qpf);

		double total_seconds_genbytecode = total_qpc_genbytecode / (double)qpf.QuadPart;
		double total_seconds_d3dcompile = total_qpc_d3dcompile / (double)qpf.QuadPart;
		double seconds_per_genbytecode = total_seconds_genbytecode / total_calls;
		double seconds_per_d3dcompile = total_seconds_d3dcompile / total_calls;
		double ratio = total_seconds_d3dcompile / total_seconds_genbytecode;
		printf("%d inputs, genbytecode: %.1f us, d3dcompile: %.1f us, %.1fx faster\n", num_inputs,
			seconds_per_genbytecode * 1e6, seconds_per_d3dcompile * 1e6, ratio);

		long long num_permutations = 1; // pow(countof(FORMATS), num_inputs)
		for (int i = 0; i < num_inputs; i++)
			num_permutations *= _countof(FORMATS);
		
		const int MAX_PERMUTATIONS = 2000; // upper bound on iterations per #inputs otherwise it grows exponentially
		long long advance = (num_permutations + MAX_PERMUTATIONS - 1) / MAX_PERMUTATIONS;

		for (long long permutation = 0; permutation < num_permutations; permutation += advance)
		{
			long long state = permutation;
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
			total_calls += 1;
		}
	}
}
