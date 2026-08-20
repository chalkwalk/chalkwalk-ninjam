#pragma once
#include <cstdint>


namespace chalkwalk::ninjam {

class Sha1 {
public:
  Sha1();
  void add(const void *data, int len);
  void result(void *out); // writes 20 bytes; resets state
private:
  uint32_t h[5];
  uint8_t buf[64];
  uint64_t byteCount;
  void processBlock(const uint8_t *block);
};

}  // namespace chalkwalk::ninjam
