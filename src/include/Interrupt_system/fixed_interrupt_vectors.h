#pragma once
#include "stdint.h"
namespace ivec
{
static constexpr uint8_t DIVIDE_ERROR = 0;
static constexpr uint8_t DEBUG = 1;
static constexpr uint8_t NMI = 2;
static constexpr uint8_t BREAKPOINT = 3;
static constexpr uint8_t OVERFLOW = 4;
static constexpr uint8_t BOUND_RANGE_EXCEEDED = 5;
static constexpr uint8_t INVALID_OPCODE = 6;
static constexpr uint8_t DEVICE_NOT_AVAILABLE = 7;
static constexpr uint8_t DOUBLE_FAULT = 8;
static constexpr uint8_t COPROCESSOR_SEGMENT_OVERRUN = 9;
static constexpr uint8_t INVALID_TSS = 10;
static constexpr uint8_t SEGMENT_NOT_PRESENT = 11;
static constexpr uint8_t STACK_SEGMENT_FAULT = 12;
static constexpr uint8_t GENERAL_PROTECTION_FAULT = 13;
static constexpr uint8_t PAGE_FAULT = 14;
static constexpr uint8_t X87_FPU_ERROR = 16;
static constexpr uint8_t ALIGNMENT_CHECK = 17;
static constexpr uint8_t MACHINE_CHECK = 18;
static constexpr uint8_t SIMD_FLOATING_POINT_EXCEPTION = 19;
static constexpr uint8_t VIRTUALIZATION_EXCEPTION = 20;
static constexpr uint8_t CONTROL_PROTECTION_EXCEPTION = 21;
static constexpr uint8_t LAPIC_TIMER = 32;
static constexpr uint8_t IPI = 33;
static constexpr uint8_t TOP_FOR_TEMPLATE_VECS = 64;
};
