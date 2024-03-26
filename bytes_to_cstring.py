# Convert python byte array into a C string literal.
# Useful for baking data directly into C executable.
#
# The generated C string is NOT 0 terminated by default.
# If you want 0 termination, append b'\0' at the end of the input.
#
# The generated string literal is close to optimal in terms of
# source code length. It's possible to get it slightly shorter,
# but not in a way that's portable or doesn't produce compiler warnings.

def bytes_to_cstring(name: str, data: bytes, maxwidth: int) -> str:
	lines = []
	line = ''
	prevoct = False
	ESCAPE = {
		ord('\a'): '\\a',
		ord('\a'): '\\a',
		ord('\b'): '\\b',
		ord('\f'): '\\f',
		ord('\n'): '\\n',
		ord('\r'): '\\r',
		ord('\t'): '\\t',
		ord('\v'): '\\v',
		ord('\\'): '\\\\',
		ord('\"'): '\\"',
	}
	for byte in data:
		if len(line) > maxwidth:
			lines.append('\t"'+line+'"')
			line = ''
			prevoct = False
		if byte in ESCAPE:
			line += ESCAPE[byte]
			prevoct = False
		elif ord('0') <= byte <= ord('9'):
			if prevoct:
				line += '\\%o' % byte
			else:
				line += chr(byte)
		elif ord(' ') <= byte <= ord('~'):
			line += chr(byte)
			prevoct = False
		else:
			line += '\\%o' % byte
			prevoct = True
	if len(line) > 0:
		lines.append('\t"'+line+'"')
	result = ''
	result += 'static const unsigned char '+name+'['+str(len(data))+'] =\n'
	result += '\n'.join(lines)+';\n'
	return result

TEST = bytes([55, 138, 87, 147, 13, 123, 230, 172, 237, 133])
print(bytes_to_cstring('TEST', TEST, 80))