#ifndef FCAM_DEMOSAIC_ARM_H
#define FCAM_DEMOSAIC_ARM_H
//#ifdef FCAM_ARCH_ARM

#include "HalideBuffer.h"

// Arm-specific optimized post-processing routines

namespace FCam {
void demosaic_ARM(Halide::Image<uint16_t> input, Halide::Image<uint8_t> out, float colorTemp, float contrast, bool denoise, int blackLevel, int whiteLevel, float gamma);
}

//#endif
#endif
