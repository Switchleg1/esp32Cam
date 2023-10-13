#include <stdio.h>
#include <string.h>
#include "jpg2rgb.h"

bool _rgb_write(void* arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t* data)
{
    // mpjpeg2sd: mofified to generate 8 bit grayscale
    rgb_jpg_decoder * jpeg = (rgb_jpg_decoder *)arg;
    if (!data) {
        if (x == 0 && y == 0) {
            // write start
            jpeg->width = w;
            jpeg->height = h;
            // if output is null, this is BMP
            if (!jpeg->output) {
                jpeg->output = (uint8_t*)malloc((w * h) + jpeg->data_offset);
                if (!jpeg->output) {
                    return false;
                }
            }
        } 
        return true;
    }

    /*uint32_t jw = jpeg->width*RGB888_BYTES;
    uint32_t t = y * jw;
    uint32_t b = t + (h * jw);
    uint32_t l = x * RGB888_BYTES;
    uint8_t *out = jpeg->output+jpeg->data_offset;
    uint8_t *o = out;
    uint32_t iy, ix;
    w *= RGB888_BYTES;

    for (iy=t; iy<b; iy+=jw) {
        o = out+(iy+l)/RGB888_BYTES;
        for (ix=0; ix<w; ix+=RGB888_BYTES) {
            uint16_t grayscale = (data[ix+2]+data[ix+1]+data[ix])/RGB888_BYTES;
            o[ix/RGB888_BYTES] = (uint8_t)grayscale;
        }
        data+=w;
    }*/

    uint32_t pixelCnt = w * h;
    uint8_t* outBuf = jpeg->output + jpeg->data_offset + (jpeg->width * y) + x;
    uint8_t* inBuf = data;

    while (pixelCnt--) {
        uint32_t pixel = *inBuf;
        inBuf++;

        pixel += *inBuf;
        inBuf++;

        pixel += *inBuf;
        inBuf++;

        *outBuf = pixel / 3;
        outBuf++;
    }

  return true;
}

size_t _jpg_read(void* arg, size_t index, uint8_t* buf, size_t len) {
  rgb_jpg_decoder* jpeg = (rgb_jpg_decoder*)arg;
  if (buf) memcpy(buf, jpeg->input + index, len);

  return len;
}

bool jpg2rgb(const uint8_t *src, uint32_t src_len, uint8_t **out, jpg_scale_t scale)
{
  rgb_jpg_decoder jpeg;
  jpeg.width        = 0;
  jpeg.height       = 0;
  jpeg.input        = src;
  jpeg.output       = NULL; 
  jpeg.data_offset  = 0;
  esp_err_t res     = esp_jpg_decode(src_len, scale, _jpg_read, _rgb_write, (void*)&jpeg);
  *out              = jpeg.output;

  return (res == ESP_OK) ? true : false;
}
