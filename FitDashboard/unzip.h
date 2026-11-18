/*
 * File: unzip.h
 * Purpose: Unpack the first file of a ZIP archive that is held in memory.
 * Garmin delivers the FIT file of an activity this way. Uses the inflate
 * routine in the ESP32 ROM (tinfl), so it costs no flash.
 */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef UNZIP_HOST_TEST   // PC test: zlib stands in for the ROM routine
#include <zlib.h>
inline bool rawInflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
  z_stream z = {};
  if (inflateInit2(&z, -15) != Z_OK) return false;
  z.next_in = (Bytef*)in; z.avail_in = inLen; z.next_out = out; z.avail_out = outLen;
  int r = inflate(&z, Z_FINISH);
  inflateEnd(&z);
  return r == Z_STREAM_END && z.total_out == outLen;
}
inline void* unzipAlloc(size_t n) { return malloc(n); }
inline void unzipFree(void* p) { free(p); }
#else
#include <esp_heap_caps.h>
#include "miniz.h"
inline void* unzipAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
inline void unzipFree(void* p) { heap_caps_free(p); }
inline bool rawInflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
  // the decompressor state is about 11 kB: too large for the task stack
  tinfl_decompressor* d = (tinfl_decompressor*)unzipAlloc(sizeof(tinfl_decompressor));
  if (!d) return false;
  tinfl_init(d);
  size_t inSize = inLen, outSize = outLen;
  tinfl_status s = tinfl_decompress(d, in, &inSize, out, out, &outSize, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  unzipFree(d);
  return s == TINFL_STATUS_DONE && outSize == outLen;
}
#endif

inline uint32_t zipU16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t zipU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// Returns the unpacked first file (free with unzipFree) or nullptr. maxSize limits the result.
inline uint8_t* unzipFirst(const uint8_t* zip, size_t len, size_t* outLen, size_t maxSize = 4u << 20) {
  if (len < 22 + 46 + 30) return nullptr;
  // end-of-central-directory record: search backwards (a comment may follow it)
  size_t eocd = len - 22;
  size_t stop = len > 22 + 65535 ? len - 22 - 65535 : 0;
  while (zipU32(zip + eocd) != 0x06054b50) {
    if (eocd == stop) return nullptr;
    eocd--;
  }
  size_t cd = zipU32(zip + eocd + 16);
  if (cd + 46 > len || zipU32(zip + cd) != 0x02014b50) return nullptr;
  uint32_t method = zipU16(zip + cd + 10);
  size_t packed = zipU32(zip + cd + 20), size = zipU32(zip + cd + 24), local = zipU32(zip + cd + 42);
  if (local + 30 > len || zipU32(zip + local) != 0x04034b50) return nullptr;
  size_t data = local + 30 + zipU16(zip + local + 26) + zipU16(zip + local + 28);
  if (data + packed > len || size == 0 || size > maxSize) return nullptr;
  uint8_t* out = (uint8_t*)unzipAlloc(size);
  if (!out) return nullptr;
  bool ok = false;
  if (method == 0 && packed == size) { memcpy(out, zip + data, size); ok = true; }
  else if (method == 8) ok = rawInflate(zip + data, packed, out, size);
  if (!ok) { unzipFree(out); return nullptr; }
  *outLen = size;
  return out;
}
