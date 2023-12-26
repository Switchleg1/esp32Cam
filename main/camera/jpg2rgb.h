#ifndef JPG2RGB_H
#define JPG2RGB_H

#include "globals.h"

#define RGB888_BYTES 3 // number of bytes per pixel

typedef struct {
  uint16_t        width;
  uint16_t        height;
  uint16_t        data_offset;
  const uint8_t*  input;
  uint8_t*        output;
} rgb_jpg_decoder;

bool jpg2rgb(const uint8_t* src, uint32_t src_len, uint8_t** out, jpg_scale_t scale);

#endif
