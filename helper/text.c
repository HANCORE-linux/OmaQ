#include "text.h"

int omaq_utf8_bytes_ok(const uint8_t *value, size_t length, int reject_controls)
{
	size_t offset = 0;

	if (!value && length)
		return 0;
	while (offset < length) {
		uint8_t first = value[offset++];

		if (first == 0 || (reject_controls && (first < 0x20 || first == 0x7f)))
			return 0;
		if (first < 0x80)
			continue;
		if (first >= 0xc2 && first <= 0xdf) {
			if (offset >= length || value[offset] < 0x80 || value[offset] > 0xbf ||
			    (reject_controls && first == 0xc2 && value[offset] <= 0x9f))
				return 0;
			offset++;
			continue;
		}
		if (first >= 0xe0 && first <= 0xef) {
			if (offset + 1 >= length || value[offset] < 0x80 ||
			    value[offset] > 0xbf || value[offset + 1] < 0x80 ||
			    value[offset + 1] > 0xbf ||
			    (first == 0xe0 && value[offset] < 0xa0) ||
			    (first == 0xed && value[offset] > 0x9f))
				return 0;
			offset += 2;
			continue;
		}
		if (first >= 0xf0 && first <= 0xf4) {
			if (offset + 2 >= length || value[offset] < 0x80 ||
			    value[offset] > 0xbf || value[offset + 1] < 0x80 ||
			    value[offset + 1] > 0xbf || value[offset + 2] < 0x80 ||
			    value[offset + 2] > 0xbf ||
			    (first == 0xf0 && value[offset] < 0x90) ||
			    (first == 0xf4 && value[offset] > 0x8f))
				return 0;
			offset += 3;
			continue;
		}
		return 0;
	}
	return 1;
}
