/*
 * keeloq_decode.c
 *
 * Created: 27. 5. 2021. 11:22:50
 *  Author: Trax
 */ 

#include <stdio.h>

#include "keeloq_decode.h"

// Decode KeeLoq payload. Return 1 on success or 0 if CRC failed (when available)
uint8_t keeloq_decode(uint8_t *kl_buff, uint8_t kl_buff_bit_size, uint64_t key, struct KEELOQ_DECODE_PLAIN *decoded) {
	// lets process the un-encrypted portion, bytes: [7][6][5][4]
	decoded->buttons = (kl_buff[7] & 0b11110000) >> 4;

	// serial, 28 bits
	decoded->serial = 0;
	decoded->serial |= (uint32_t)(kl_buff[7] & 0b00001111) << 24;
	decoded->serial |= (uint32_t)kl_buff[6] << 16;
	decoded->serial |= (uint32_t)kl_buff[5] << 8;
	decoded->serial |= kl_buff[4];

	// lets process the encrypted portion, bytes: [3][2][1][0]
	// if key is available then decrypt and extract data
	if(key) {
		uint32_t encrypted = 0;
		encrypted |= (uint32_t)kl_buff[3] << 24;
		encrypted |= (uint32_t)kl_buff[2] << 16;
		encrypted |= (uint16_t)kl_buff[1] << 8;
		encrypted |= kl_buff[0];

		// decrypt
		keeloq_decrypt(&encrypted, &key);

		// decrypted buttons
		decoded->buttons_enc = (uint8_t)(encrypted >> 28) & 0x0F;

		// discrimination, 12 bits
		decoded->disc = (uint16_t)(encrypted >> 16) & 0x0FFF;

		// counter
		decoded->counter = (uint16_t)(encrypted & 0xFFFF);

		// post-decryption checks should be carried out in the user-application (discrimination and buttons from the ecnrypted section)
	}
	else {
		// since no key is provided, it is probably HCS101 which has different format of this portion
		decoded->counter = (uint16_t)(kl_buff[3] << 8) | kl_buff[2];
		decoded->disc = 0; // no disc in this case
		decoded->buttons_enc = (kl_buff[1] >> 4) & 0x0F;

		// take serial3 also
		decoded->serial3 = ((uint16_t)(kl_buff[1] << 8) | kl_buff[0]) & 0x03FF;
	}
	
	// Vlow bit
	decoded->vlow = kl_buff[8] & 0b00000001;
	
	// decoded 65 bits so far

	if(kl_buff_bit_size == 66) {
		// "Repeat" bit
		decoded->repeat = kl_buff[8] & 0b00000010;

		// decoded 66 bits so far
	}
	
	// HCS360 361 362
	if(kl_buff_bit_size >= 67) {
		uint8_t received_crc = (kl_buff[8] & 0b00000110) >> 1;
		uint8_t calc_crc = keeloq_decode_calc_crc(kl_buff);

		decoded->crc = received_crc;

		if(received_crc != calc_crc) {
			return 0; // failure
		}
		
		// decoded 67 bits so far
	}

	// HCS 362
	if(kl_buff_bit_size == 69) {
		decoded->que = (kl_buff[8] & 0b00011000) >> 3;

		// decoded 69 bits so far
	}
	
	return 1; // good
}

// Encode KeeLoq payload
void keeloq_encode(uint8_t encoder, struct KEELOQ_DECODE_PLAIN *decoded, uint64_t key, uint8_t *kl_buff) {
	// rolling-code encoder
	if(key) {
		// counter value
		uint32_t encrypted_section = decoded->counter;
	
		// discrimination value
		if(encoder == ENCODER_HCS360 ||encoder == ENCODER_HCS361) {
			encrypted_section |= (uint32_t)decoded->serial << 16;
		}
		else {
			encrypted_section |= (uint32_t)decoded->disc << 16;
		}
	
		// add button states to 4 MSBs
		encrypted_section &= 0x0FFFFFFF;
		encrypted_section |= (uint32_t)(decoded->buttons & 0b00001111) << 28;
	
		keeloq_encrypt(&encrypted_section, &key);
		
		// add to buffer
		kl_buff[3] = (uint8_t)(encrypted_section >> 24);
		kl_buff[2] = (uint8_t)(encrypted_section >> 16);
		kl_buff[1] = (uint8_t)(encrypted_section >> 8);
		kl_buff[0] = (uint8_t)encrypted_section;
	}
	// probably HCS101. it has a different "encrypted" section which is not encrypted, and also "SERIAL 3"
	else {
		// counter value
		kl_buff[3] = (uint8_t)(decoded->counter & 0xFF);
		kl_buff[2] = (uint8_t)((decoded->counter & 0xFF00) >> 8);

		// put serial 3 in last 2 bytes, but overwrite with buttons and "00 bits" later
		kl_buff[1] = (uint8_t)((decoded->serial3 & 0xFF00) >> 8);
		kl_buff[0] = (uint8_t)decoded->serial3;

		// "00 bits" and buttons again
		kl_buff[1] &= 0b00000011;
		kl_buff[1] |= ((decoded->buttons & 0b00001111) << 4);
	}
		
	// build fixed portion next
	
	// buttons & serial number (serial is 28 bits so overwrite upper 4 bits with ->buttons lower nibble)
	kl_buff[7] = ((decoded->buttons & 0b00001111) << 4) | ((uint8_t)(decoded->serial >> 24) & 0b00001111);
	kl_buff[6] = (uint8_t)(decoded->serial >> 16);
	kl_buff[5] = (uint8_t)(decoded->serial >> 8);
	kl_buff[4] = (uint8_t)(decoded->serial);

	kl_buff[8] = 0;

	// Vlow
	kl_buff[8] |= decoded->vlow > 0;

	// so far we have envoded 65 bits

	// add CRC
	if(encoder == ENCODER_HCS360 || encoder == ENCODER_HCS361 || encoder == ENCODER_HCS362) {
		uint8_t crc = keeloq_decode_calc_crc(kl_buff);
		kl_buff[8] |= crc << 1;
		// 67 bits so far
	}
	// for HCS101 this bit is always "1"
	else if(encoder == ENCODER_HCS101) {
		kl_buff[8] |= 0b00000001 << 1;
		// 66 bits so far
	}
	// add Repeat bit for others
	else {
		kl_buff[8] |= (decoded->repeat > 0) << 1;
		// 66 bits so far
	}

	if(encoder == ENCODER_HCS362) {
		kl_buff[8] |= (decoded->que) << 3;
		// 69 bits so far
	}
}

// public
void keeloq_decode_build_prog_stream(uint8_t *stream, struct KEELOQ_DECODE_PROG_PROFILE *prog_profile) {
	// they all start the same

	// key (64bit / 8bytes) (LSb ... MSb)
	memcpy(stream, (uint32_t *)&prog_profile->key, 8);
	stream += 8;
	
	// sync (16bit / 2bytes)
	memcpy(stream, (uint16_t *)&prog_profile->sync, 2);
	stream += 2;

	// this is where they become different
	if(prog_profile->encoder == ENCODER_HCS360 || prog_profile->encoder == ENCODER_HCS361) {
		// seed2 (16bit / 2 bytes) (we will not bother with sync_B independent feature, I don't even have these encoders here to test)
		memcpy(stream, (uint16_t *)&prog_profile->seed2, 2);
		stream += 4;
		
		// reserved 2 bytes (0x0000)
		*stream = 0;
		stream++;
		*stream = 0;
		stream++;

		// seed (32bit / 4 bytes)
		memcpy(stream, (uint32_t *)&prog_profile->seed, 4);
		stream += 4;

		// serial (32bit / 4bytes) ... only 24 LSB are used though
		memcpy(stream, (uint32_t *)&prog_profile->ser, 4);
		stream += 4;
	}
	// for others...
	else {
		// reserved 2 bytes (0x0000)
		*stream = 0;
		stream++;
		*stream = 0;
		stream++;

		// serial (32bit / 4bytes) ... only 24 LSB are used though
		memcpy(stream, (uint32_t *)&prog_profile->ser, 4);
		stream += 4;
	
		// seed (32bit / 4 bytes)
		memcpy(stream, (uint32_t *)&prog_profile->seed, 4);
		stream += 4;
		
		// for HCS201 this is the discrimination word
		if(prog_profile->encoder == ENCODER_HCS201) {
			// disc (16bit / 2bytes)
			memcpy(stream, (uint16_t *)&prog_profile->disc_hcs201, 2);
			stream += 2;
		}
		// for others it is the RESERVED
		else {
			*stream = 0;
			stream++;
			*stream = 0;
			stream++;
		}
	}
	
	// config word for all
	memcpy(stream, (uint16_t *)&prog_profile->config, 2);
}

// private

// calculate CRC over an entire 65 bits
uint8_t keeloq_decode_calc_crc(uint8_t *kl_buff) {
	uint8_t crc1 = 0;
	uint8_t crc0 = 0;
	
	for(uint8_t kl_buff_bit_index = 0; kl_buff_bit_index < 66; kl_buff_bit_index++) {
		uint8_t arr_index = kl_buff_bit_index / 8;
		uint8_t arr_bit_index = kl_buff_bit_index % 8;		
		uint8_t buff_byteval = kl_buff[arr_index];

		if(arr_bit_index > 0) {
			buff_byteval = (buff_byteval >> arr_bit_index);
		}

		uint8_t temp = crc1;
		crc1 = crc0 ^ (buff_byteval & 0b00000001);
		crc0 = crc1 ^ temp;
	}
	
	return (crc1 << 1) | crc0;
}
