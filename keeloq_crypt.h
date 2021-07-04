/*
 * keeloq_crypt.h
 *
 * Created: 15. 3. 2021. 18:40:38
 *  Author: Trax
 */ 

#ifndef KEELOQ_CRYPT_H_
#define KEELOQ_CRYPT_H_

#include <stdio.h>

void keeloq_decrypt(uint32_t *, uint64_t *);
void keeloq_encrypt(uint32_t *, uint64_t *);

#endif /* KEELOQ_CRYPT_H_ */
