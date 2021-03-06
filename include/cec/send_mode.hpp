#pragma once

#include <string>

namespace streetpass::cec {
class SendMode {
 public:
  enum send_mode : uint8_t {
    EXCHANGE = 0x0,
    RECV_ONLY = 0x1,
    SEND_ONLY = 0x2,
    SEND_RECV = 0x3,
  };

  constexpr SendMode(send_mode mode) : m_value(mode) {}

  operator send_mode() const { return m_value; }
  explicit operator bool() = delete;
  explicit operator std::string() const;

  bool match(SendMode const& other) const;

  friend std::ostream& operator<<(std::ostream& s, const SendMode& m);

 private:
  send_mode m_value;
};
}  // namespace streetpass::cec
