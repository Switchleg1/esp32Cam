#ifndef JPG2RGB_H
#define JPG2RGB_H

#include <esp_camera.h>

#define RGB888_BYTES 3 // number of bytes per pixel

typedef struct {
  uint16_t        width;
  uint16_t        height;
  uint16_t        data_offset;
  const uint8_t*  input;
  uint8_t*        output;
} rgb_jpg_decoder;

bool     _rgb_write(void * arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data);
uint32_t _jpg_read(void * arg, size_t index, uint8_t *buf, size_t len);
bool     jpg2rgb(const uint8_t *src, size_t src_len, uint8_t **out, jpg_scale_t scale);

#endif
