#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace streetpass::ioctl {
class Socket {
 private:
  int sock;

 public:
  Socket();
  ~Socket();
  int get_fd() const;
};

bool is_interface_up(Socket const& socket, std::string const& if_name);
void set_interface_up(Socket const& socket, std::string const& if_name);
void set_interface_down(Socket const& socket, std::string const& if_name);
std::array<std::uint8_t, 6> get_interface_hwaddr(Socket const& socket,
                                                 std::string const& if_name);
void set_interface_hwaddr(Socket const& socket, std::string const& if_name,
                          std::array<std::uint8_t, 6> const& addr);
int get_interface_index(Socket const& socket, std::string const& if_name);
}  // namespace streetpass::ioctl
