/*
 * ee_db.c
 *
 * Created: 2. 5. 2021. 20:51:38
 *  Author: Trax
 */ 

#include "ee_db.h"

#include "lib/uart/uart.h"

// NOTE: you need to init the i2c hardware externally
void eedb_init_ctx(volatile struct eedb_ctx *ctx) {
	//uart_puts("eedb_init_ctx()\r\n");
	
	// check if memory is formatted, and if not - format now. if yes - we should not touch it!
	eedb_read_i2c(ctx, ctx->start_eeaddr, sizeof(struct eedb_info), (struct eedb_info *)&(ctx->_eedb_info));

	ctx->_block_size = sizeof(struct eedb_record_header) + ctx->sizeof_record_entry;
	ctx->_allocated_bytes_eeaddr = sizeof(struct eedb_info) + (ctx->record_capacity * (sizeof(struct eedb_record_header) + ctx->sizeof_record_entry));
	ctx->_next_free_eeaddr = ctx->start_eeaddr + ctx->_allocated_bytes_eeaddr;

	/*char tmp[64];
	sprintf(tmp, "RECORD CAPACITY: %u\r\n", ctx->record_capacity);
	uart_puts(tmp);
	sprintf(tmp, "ALLOCATED: %u\r\n", ctx->_allocated_bytes_eeaddr);
	uart_puts(tmp);
	sprintf(tmp, "SIZEOF REC-ENTRY: %u\r\n", ctx->sizeof_record_entry);
	uart_puts(tmp);
	sprintf(tmp, "BLOCK SIZE: %u\r\n", ctx->_block_size);
	uart_puts(tmp);
	sprintf(tmp, "NEXT FREE EEADDR: %u\r\n", ctx->_next_free_eeaddr);
	uart_puts(tmp);*/
	
	if(ctx->_eedb_info.formatted_magic != EEDB_FORMATTED_MAGIC) {
		//uart_puts("EEDB INVALID.\r\n");
		eedb_format_memory(ctx);
	}
	else {
		//uart_puts("EEDB VALID.\r\n");
		//eedb_invalidate_cache(ctx);
		ctx->_next_free_header_entry_eeaddr = EEDB_INVALID_ADDR; // we don't know this yet
	}
}

/*
void eedb_invalidate_cache(volatile struct eedb_ctx *ctx) {
	for(uint8_t i=0; i<EEDB_CACHE_SIZE; i++) {
		ctx->_record_cache[i].valid = 0;
	}
	ctx->_cache_element_index = 0;
}
*/

// save info section into EEPROM memory
void eedb_update_info(volatile struct eedb_ctx *ctx, struct eedb_info *info) {
	//uart_puts("eedb_update_info()\r\n");
	
	eedb_write_n_i2c(ctx, ctx->start_eeaddr, sizeof(struct eedb_info), info);
}

// format entire memory
void eedb_format_memory(volatile struct eedb_ctx *ctx) {
	//uart_puts("eedb_format_memory()\r\n");
	
	// lets skip the eedb_info, so the next free location will be the first one in memory space
	ctx->_next_free_header_entry_eeaddr = ctx->start_eeaddr + sizeof(struct eedb_info);
	//eedb_invalidate_cache(ctx);

	//ctx->_eedb_info.record_capacity = 0;

	// mark all records as deleted in their header section
	struct eedb_record_header header_entry;
	header_entry.deleted = 1;
	header_entry.fk = 0;
	header_entry.pk = 0;
	// skip eedb_info section, jump block by block
	for(
			uint16_t eeaddr = ctx->start_eeaddr + sizeof(struct eedb_info);
			eeaddr <= ctx->start_eeaddr + ctx->_allocated_bytes_eeaddr - ctx->_block_size;
			eeaddr += ctx->_block_size
	) {
		eedb_write_n_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), &header_entry);
		//ctx->_eedb_info.record_capacity++;
	}
	
	ctx->_eedb_info.formatted_magic = EEDB_FORMATTED_MAGIC;
	eedb_update_info(ctx, (struct eedb_info *)&(ctx->_eedb_info));
}

// read record header and record data by absolute EEPROM address
void eedb_read_record_by_eeaddr(volatile struct eedb_ctx *ctx, uint16_t eeaddr, struct eedb_record_header *header_entry, void *dest) {
	if (header_entry != 0) {
		eedb_read_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), header_entry);
	}
	if (dest != 0) {
		eedb_read_i2c(ctx, eeaddr + sizeof(struct eedb_record_header), ctx->sizeof_record_entry, dest);
	}
}

// write/replace record header and record data by absolute EEPROM address
void eedb_write_record_by_eeaddr(volatile struct eedb_ctx *ctx, uint16_t eeaddr, struct eedb_record_header *header_entry, void *src) {
	if (header_entry != 0) {
		eedb_write_n_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), header_entry);
	}
	if (src != 0) {
		eedb_write_n_i2c(ctx, eeaddr + sizeof(struct eedb_record_header), ctx->sizeof_record_entry, src);
	}
}

// finds first free record EEPROM address
uint16_t eedb_find_free_record_eeaddr(volatile struct eedb_ctx *ctx) {
	//uart_puts("eedb_find_free_record_eeaddr()\r\n");
	
	for(
		uint16_t eeaddr = ctx->start_eeaddr + sizeof(struct eedb_info);
		eeaddr <= ctx->start_eeaddr + ctx->_allocated_bytes_eeaddr - ctx->_block_size;
		eeaddr += ctx->_block_size
	) {
		struct eedb_record_header header_entry;
		eedb_read_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), &header_entry);

		// found it?
		if(header_entry.deleted) {
			return eeaddr;
		}
	}
	
	// no more free memory
	return EEDB_INVALID_ADDR;
}

// update record into the database. 1 = success, 0 = record not found
uint8_t eedb_update_record(volatile struct eedb_ctx *ctx, uint32_t pk, uint32_t fk, uint16_t skip_eeaddr, struct eedb_record_header *header, void *record) {
	uint16_t eeaddr = eedb_find_record_eeaddr(ctx, pk, fk, skip_eeaddr);
	if(eeaddr == EEDB_INVALID_ADDR) {
		return 0; // record not found
	}

	// replace the header section if arrived
	if(header) {
		eedb_write_n_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), header);
	}

	// replace the record section if arrived
	if(record) {
		eedb_write_n_i2c(ctx, eeaddr + sizeof(struct eedb_record_header), ctx->sizeof_record_entry, record);
	}

	return 1;
}

// inserts record into database. EEADDR = success, EEDB_INVALID_ADDR = no more free memory
uint16_t eedb_insert_record(volatile struct eedb_ctx *ctx, uint32_t pk, uint32_t fk, void *new_record) {
	//uart_puts("eedb_insert_record()\r\n");
	
	uint16_t written_eeaddr = ctx->_next_free_header_entry_eeaddr;
	ctx->_next_free_header_entry_eeaddr = EEDB_INVALID_ADDR; // we will/used it up
	
	// find next free location if we don't know it already
	if(written_eeaddr == EEDB_INVALID_ADDR) {
		written_eeaddr = eedb_find_free_record_eeaddr(ctx);
		// no more free memory?
		if(written_eeaddr == EEDB_INVALID_ADDR) {
			return EEDB_INVALID_ADDR;
		}
	}

	// prepare header for this new record
	struct eedb_record_header header_entry;
	header_entry.pk = pk;
	header_entry.fk = fk;
	header_entry.deleted = 0;

	// save it
	eedb_write_record_by_eeaddr(ctx, written_eeaddr, &header_entry, new_record);

	return written_eeaddr;
}

// update if exists, insert if does not exist in db. return 1 on update, 0 on insert
uint8_t eedb_upsert_record(volatile struct eedb_ctx *ctx, uint32_t pk, uint32_t fk, uint16_t skip_eeaddr, void *record) {
	uint16_t eeaddr = eedb_find_record_eeaddr(ctx, pk, fk, skip_eeaddr);
	if(eeaddr == EEDB_INVALID_ADDR) {
		eedb_insert_record(ctx, pk, fk, record);
		return 0;
	}
	else {
		eedb_update_record(ctx, pk, fk, skip_eeaddr, 0, record);
		return 1;
	}
}

// delete record from the database. 1 = success, 0 = record not found
uint8_t eedb_delete_record(volatile struct eedb_ctx *ctx, uint32_t pk, uint32_t fk, uint16_t skip_eeaddr) {
	uint16_t eeaddr = eedb_find_record_eeaddr(ctx, pk, fk, skip_eeaddr);
	if(eeaddr == EEDB_INVALID_ADDR) {
		return 0; // record not found
	}
	
	// lets just mark record as deleted
	struct eedb_record_header header_entry;
	eedb_read_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), &header_entry);
	header_entry.deleted = 1;
	eedb_write_n_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), &header_entry);

	// we know where next free location is
	ctx->_next_free_header_entry_eeaddr = eeaddr;

	return 1;
}

// finds first record to satisfy condition of PK or FK
uint16_t eedb_find_record_eeaddr(volatile struct eedb_ctx *ctx, uint32_t pk, uint32_t fk, uint16_t skip_eeaddr) {
	//uart_puts("eedb_find_record_eeaddr()\r\n");
	
	if(!pk && !fk) return EEDB_INVALID_ADDR;

	// need to skip last read address? move to next block then
	uint16_t eeaddr_offset = 0;
	if(skip_eeaddr) {
		eeaddr_offset = skip_eeaddr - ctx->start_eeaddr - sizeof(struct eedb_info) + ctx->_block_size;
	}

	/*char tmp[64];
	sprintf(tmp, "START = %u\r\n", (ctx->start_eeaddr + sizeof(struct eedb_info) + eeaddr_offset));
	uart_puts(tmp);
	sprintf(tmp, "START <= %u\r\n", (ctx->start_eeaddr + ctx->_allocated_bytes_eeaddr - ctx->_block_size));
	uart_puts(tmp);
	sprintf(tmp, "BLOCKSIZE = %u\r\n", ctx->_block_size);
	uart_puts(tmp);*/

	for(
		uint16_t eeaddr = ctx->start_eeaddr + sizeof(struct eedb_info) + eeaddr_offset;
		eeaddr <= ctx->start_eeaddr + ctx->_allocated_bytes_eeaddr - ctx->_block_size;
		eeaddr += ctx->_block_size
	) {
		/*sprintf(tmp, "seek @ %u\r\n", eeaddr);
		uart_puts(tmp);*/

		struct eedb_record_header header_entry;
		eedb_read_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), &header_entry);

		// lets find next free memory location in advance
		if(ctx->_next_free_header_entry_eeaddr == EEDB_INVALID_ADDR && header_entry.deleted) {
			ctx->_next_free_header_entry_eeaddr = eeaddr;
			continue; // this one is deleted, so skip it
		}

		if(
			!header_entry.deleted
			&&
			(
				(pk && (pk == EEDB_PKFK_ANY || pk == header_entry.pk))
				||
				(fk && (fk == EEDB_PKFK_ANY || fk == header_entry.fk))
			)
		) {
			return eeaddr;
		}
	}

	//uart_puts("eedb_find_record_eeaddr() - not found\r\n");
	
	return EEDB_INVALID_ADDR;
}

// calls callback for each matched record according to PK and FK
uint16_t eedb_for_each_record(volatile struct eedb_ctx *ctx, uint32_t pk, uint32_t fk, void (*callback)(volatile struct eedb_ctx *, struct eedb_record_header *, void *), struct eedb_record_header *header, void *record) {
	uint16_t processed_count = 0;
	uint16_t eeaddr = 0;
	
	while(1) {	
		eeaddr = eedb_find_record_eeaddr(ctx, pk, fk, eeaddr);

		// done?
		if(eeaddr == EEDB_INVALID_ADDR) {
			break;
		}

		eedb_read_record_by_eeaddr(ctx, eeaddr, header, record);
		callback(ctx, header, record);

		processed_count++;
	}

	return processed_count;
}

/*
// DEBUG
void eedb_print_headers(volatile struct eedb_ctx *ctx) {
	//uart_puts("eedb_print_headers()\r\n");

	for(
		uint16_t eeaddr = ctx->start_eeaddr + sizeof(struct eedb_info);
		eeaddr <= ctx->start_eeaddr + ctx->_allocated_bytes_eeaddr - ctx->_block_size;
		eeaddr += ctx->_block_size
	) {
		struct eedb_record_header header_entry;
		eedb_read_i2c(ctx, eeaddr, sizeof(struct eedb_record_header), &header_entry);
		
		char tmp[127];
		sprintf(tmp, "0x%04X; PK=%lu, DEL=%u\r\n", eeaddr, header_entry.pk, header_entry.deleted);
		uart_puts(tmp);
		
		_delay_ms(5);
	}
}
*/
		
/*
// validate/invalidate cached element by matching the pk
void eedb_update_cache_entry_by_pk(volatile struct eedb_ctx *ctx, uint32_t pk, uint8_t valid) {
	for(uint8_t i=0; i<EEDB_CACHE_SIZE; i++) {
		if(ctx->_record_cache[i].pk == pk) {
			ctx->_record_cache[i].valid = valid;
			break;
		}
	}
}

// tries to find address of a record by pk or fk
uint16_t eedb_find_in_cache(volatile struct eedb_ctx *ctx, uint8_t which_key, uint32_t key) {
	for(uint8_t i=0; i<EEDB_CACHE_SIZE; i++) {
		if((which_key == 0 && ctx->_record_cache[i].pk == key) || (which_key == 1 && ctx->_record_cache[i].fk == key)) {
			return ctx->_record_cache[i].eeaddr;
		}
	}
	
	return EEDB_INVALID_ADDR;
}

void eedb_add_to_cache(volatile struct eedb_ctx *ctx, struct eedb_cache_element *cache_element) {
	if(++ctx->_cache_element_index >= EEDB_CACHE_SIZE) {
		ctx->_cache_element_index = 0;
	}
	cache_element->valid = 1;
	ctx->_record_cache[ctx->_cache_element_index] = cache_element;	
}

// read data from cache, and if it is not available, read from i2c, cache it
// it might also be partially cached, so take what can be taken from cache and cache the next block
void eedb_read_record(volatile struct eedb_ctx *ctx, uint32_t pk, uint16_t len, void *dest) {
	//char tmp[100];
	//sprintf(tmp, "ACCESS: addr=%u, len=%u\r\n", addr, len);
	//uart_puts(tmp);

	// wanted key available in cache? we read the address, so we don't need to seek in eeprom
	uint16_t eeaddr = 0xFFFF; // invalid eeprom address
	uint8_t invalid_cache_index = EEDB_CACHE_SIZE; // location of cache index is found but as invalid. we will need this to re-cache into the same location. currently it is set to EEDB_CACHE_SIZE which is an invalid location obviously
	for(uint8_t i=0; i<EEDB_CACHE_SIZE; i++) {
		if(ctx->_record_cache[i].pk == pk) {
			if(!ctx->_record_cache[i].valid) {
				invalid_cache_index = i;
				break; // found, but not valid (probably anymore)
			}
			// it is actually valid, take it and break out
			eeaddr = ctx->_record_cache[i].eeaddr;
			break;
		}
	}
	// find next invalid cache index to cache this one
	// if not available, kill oldest one
}
*/

// read data directly from eeprom, avoiding cache
void eedb_read_i2c(volatile struct eedb_ctx *ctx, uint16_t addr, uint16_t len, void *dest) {
	// start the write
	ctx->fn_i2c_start(ctx->i2c_addr & 0b11111110);
	
	// send data address MSB LSB if eeprom is 2-byte sized
	if(EEDB_EEPROM_ADDR_SIZE > 1) {
		ctx->fn_i2c_tx((uint8_t)(addr >> 8));
	}
	ctx->fn_i2c_tx((uint8_t)(addr & 0x00FF));
	
	// start the read
	ctx->fn_i2c_start(ctx->i2c_addr | 0b00000001);
	
	// read until read
	while(len > 0) {
		// last byte read should be NACKed
		if(len == 1) {
			*((uint8_t *)dest) = ctx->fn_i2c_rx_nack();
		}
		// all others before the last one ACKed
		else {
			*((uint8_t *)dest) = ctx->fn_i2c_rx_ack();
		}
		len--;
		dest++;
	}
	
	ctx->fn_i2c_stop();
}

void eedb_write_i2c(volatile struct eedb_ctx *ctx, uint16_t addr, uint8_t data) {
	// start the write
	ctx->fn_i2c_start(ctx->i2c_addr & 0b11111110);
	
	// send data address MSB LSB if eeprom is 2-byte sized
	if(EEDB_EEPROM_ADDR_SIZE > 1) {
		ctx->fn_i2c_tx((uint8_t)(addr >> 8));
	}
	ctx->fn_i2c_tx((uint8_t)(addr & 0xFF));

	// data
	ctx->fn_i2c_tx(data);
	
	ctx->fn_i2c_stop();
}

void eedb_write_n_i2c(volatile struct eedb_ctx *ctx, uint16_t addr, uint16_t len, void *data) {
	/*char tmp[128];
	sprintf(tmp, "eedb_write_n_i2c(%u, %u, data)\r\n", addr, len);
	uart_puts(tmp);*/
	
	// start the write
	ctx->fn_i2c_start(ctx->i2c_addr & 0b11111110);
	
	// send data address MSB LSB if eeprom is 2-byte sized
	if(EEDB_EEPROM_ADDR_SIZE > 1) {
		ctx->fn_i2c_tx((uint8_t)(addr >> 8));
	}
	ctx->fn_i2c_tx((uint8_t)(addr & 0xFF));

	// data stream
	uint16_t start_addr_msb = addr & 0x8080; 
	while(len-- > 0) {
		ctx->fn_i2c_tx(*((uint8_t *)data));
		data++;
		
		addr++; // keep track of address for page rollover
		// rollover?
		if((addr & 0x8080) != start_addr_msb) {
			//uart_puts("eedb_write_n_i2c ADDR ROLLOVER\r\n");

			// restart the write procedure, again in addr as we advanced it too
			ctx->fn_i2c_stop(); // end current stream
			
			_delay_ms(4); // Tw, write delay time
			
			// re-start the write
			ctx->fn_i2c_start(ctx->i2c_addr & 0b11111110);
	
			// send data address MSB LSB if eeprom is 2-byte sized
			if(EEDB_EEPROM_ADDR_SIZE > 1) {
				ctx->fn_i2c_tx((uint8_t)(addr >> 8));
			}
			ctx->fn_i2c_tx((uint8_t)(addr & 0xFF));
		}
	}
	
	//uart_puts("eedb_write_n_i2c RETURN\r\n");
	
	ctx->fn_i2c_stop();
	_delay_ms(4); // Tw, write delay time
}
