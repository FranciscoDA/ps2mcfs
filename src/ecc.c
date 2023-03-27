/**
 * This hamming code implementation was heavily based on the implementationn in the `mymc` project by Ross Ridge
 * A copy of the project maintained by the ps2sdk devs can be found at https://github.com/ps2dev/mymc
*/

#include <stdbool.h>
#include <stdint.h>
#include "ecc.h"

// returns 0 if an even number of bits is set. returns 1 of an odd number of bits is set
bool ecc_byte_parity(uint8_t x) {
    x ^= x >> 1;
    x ^= x >> 2;
    x ^= x >> 4;
    return x & 1;
}

uint8_t ecc_column_parity_mask(uint8_t x) {
    return ecc_byte_parity(x & 0x55) << 0 // 0b01010101
        | ecc_byte_parity(x & 0x33) << 1  // 0b00110011
        | ecc_byte_parity(x & 0x0F) << 2  // 0b00001111
        | ecc_byte_parity(x & 0x00) << 3  // 0b00000000
        | ecc_byte_parity(x & 0xAA) << 4  // 0b10101010
        | ecc_byte_parity(x & 0xCC) << 5  // 0b11001100
        | ecc_byte_parity(x & 0xF0) << 6; // 0b11110000
}



void ecc128_calculate(uint8_t* ecc_dest, uint8_t* data_src) {
    uint8_t* column_parity = ecc_dest;
    uint8_t* line_parity_0 = ecc_dest+1;
    uint8_t* line_parity_1 = ecc_dest+2;

    *column_parity = 0x77; // 0b01110111
    *line_parity_0 = 0x7F; // 0b01111111
    *line_parity_1 = 0x7F; // 0b01111111

    for (unsigned i = 0; i < 128; ++i) {
        *column_parity ^= ecc_column_parity_mask(data_src[i]);
        if (ecc_byte_parity(data_src[i]) != 0) { // if is odd
            *line_parity_0 ^= ~i;
            *line_parity_1 ^= i;
        }
    }
    *line_parity_0 &= 0x7F;
}

bool ecc128_check(uint8_t* ecc_src, uint8_t* data_src) {
    // TODO: perform actual verification and error correction
    uint8_t calculated[3];
    ecc128_calculate(calculated, data_src);
    return calculated[0] == ecc_src[0]
        && calculated[1] == ecc_src[1]
        && calculated[2] == ecc_src[2];
}


void ecc512_calculate(uint8_t* ecc_dest, uint8_t* data_src) {
    for (unsigned i = 0; i < 512/128; ++i) {
        ecc128_calculate(ecc_dest + i * 3, data_src + i * 128);
    }
}

bool ecc512_check(uint8_t* ecc_src, uint8_t* data_src) {
    for (unsigned i = 0; i < 512/128; ++i) {
        if (!ecc128_check(ecc_src + i * 3, data_src + i * 128))
            return false;
    }
    return true;
}