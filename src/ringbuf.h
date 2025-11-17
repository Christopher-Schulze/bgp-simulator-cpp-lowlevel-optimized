#pragma once
template<typename T, size_t CAP>
class RingBuf {
  alignas(64) T buf[CAP];
  uint32_t head = 0, tail = 0;
public:
  bool push(const T& item) {
    uint32_t next = (head + 1) % CAP;
    if (next == tail) return false;
    buf[head] = item;
    head = next;
    return true;
  }
  bool pop(T& item) {
    if (head == tail) return false;
    item = buf[tail];
    tail = (tail + 1) % CAP;
    return true;
  }
  void reset() { head = tail = 0; }
};
