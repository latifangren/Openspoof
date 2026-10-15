#pragma once

#include <stdint.h>

typedef unsigned char byte_t;
typedef unsigned int uint;

typedef uint32_t arm_inst_t;
typedef uint16_t thumb1_inst_t;
typedef uint32_t thumb2_inst_t;
typedef uint32_t arm64_inst_t;

#ifndef NULL
#define NULL 0
#endif
