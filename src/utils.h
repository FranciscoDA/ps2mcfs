#ifndef _UTILS_H_
#define _UTILS_H_

#define div_ceil(x, y) ((x)/(y) + ((x)%(y) != 0))

#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)<(b) ? (b) : (a))

#ifdef DEBUG
#define DEBUG_printf(...) printf(__VA_ARGS__)
#else
#define DEBUG_printf(...)
#endif

#endif