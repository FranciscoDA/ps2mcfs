#ifndef __ECC_H__
#define __ECC_H__

#include <stdlib.h>
#include <stdint.h>

// data size is always 128 byte (i.e: page chunk size)

// reads the 128 bytes of data pointed at by `data_src` and writes the 3-byte hamming code in `ecc_dest`
void ecc128_calculate(uint8_t* ecc_dest, uint8_t* data_src);

// verifies the 3-byte hamming code pointed at by `ecc_src` against the 128 bytes of data pointed at by `data_src`
bool ecc128_check(uint8_t* ecc_src, uint8_t* data_src);

// calculates the ecc bytes for a whole page
void ecc512_calculate(uint8_t* ecc_dest, uint8_t* data_src);

// verifies the ecc bytes for a whole page
bool ecc512_check(uint8_t* ecc_src, uint8_t* data_src);

#endif
