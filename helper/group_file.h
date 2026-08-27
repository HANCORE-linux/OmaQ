#ifndef OMAQ_GROUP_FILE_H
#define OMAQ_GROUP_FILE_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_GROUP_FILE_ID_BYTES 16
#define OMAQ_GROUP_FILE_ID_HEX 32
#define OMAQ_GROUP_FILE_PACKET_MAX 1373
#define OMAQ_GROUP_FILE_DATA_MAX 1343

#define OMAQ_GROUP_FILE_OFFER 1
#define OMAQ_GROUP_FILE_ACCEPT 2
#define OMAQ_GROUP_FILE_CANCEL 3
#define OMAQ_GROUP_FILE_DATA 4
#define OMAQ_GROUP_FILE_DONE 5
#define OMAQ_GROUP_FILE_ACK 6
#define OMAQ_GROUP_FILE_FAIL 7

typedef struct {
	uint8_t id[OMAQ_GROUP_FILE_ID_BYTES];
	uint64_t size;
	uint8_t hash[32];
	char kind[6];
	char name[129];
} omaq_group_file_offer;

int omaq_group_file_id_hex(const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES],
			   char out[OMAQ_GROUP_FILE_ID_HEX + 1]);
int omaq_group_file_id_parse(const char *id,
			     uint8_t out[OMAQ_GROUP_FILE_ID_BYTES]);
int omaq_group_file_offer_pack(uint8_t *out, size_t outn,
			       const omaq_group_file_offer *offer);
int omaq_group_file_offer_unpack(const uint8_t *packet, size_t length,
				 omaq_group_file_offer *offer);
int omaq_group_file_control_pack(uint8_t *out, size_t outn, uint8_t type,
				 const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES]);
int omaq_group_file_control_unpack(const uint8_t *packet, size_t length,
				   uint8_t *type,
				   uint8_t id[OMAQ_GROUP_FILE_ID_BYTES]);
int omaq_group_file_data_pack(uint8_t *out, size_t outn,
			      const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES],
			      uint64_t offset, const uint8_t *data, size_t length);
int omaq_group_file_data_unpack(const uint8_t *packet, size_t length,
				uint8_t id[OMAQ_GROUP_FILE_ID_BYTES],
				uint64_t *offset, const uint8_t **data,
				size_t *data_length);
/* Returns 0 for a new durable reservation, 1 for an existing id, -1 on error. */
int omaq_group_file_id_reserve(const char *state,
			       const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES]);

#endif
