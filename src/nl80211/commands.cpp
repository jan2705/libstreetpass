#include "nl80211/commands.hpp"

#include <net/if.h>

#include <future>

#include "nl80211/message.hpp"

namespace streetpass::nl80211::commands {

namespace {

bool parse_wiphy_message(Attributes& msg_attrs, void* arg) {
  auto w = static_cast<struct wiphy*>(arg);

  w->index = msg_attrs.get<std::uint32_t>(NL80211_ATTR_WIPHY).value();
  w->name = msg_attrs.get<std::string>(NL80211_ATTR_WIPHY_NAME).value();

  auto cmd_attrs =
      msg_attrs.get<Attributes>(NL80211_ATTR_SUPPORTED_COMMANDS).value();
  for (auto attr_type : cmd_attrs.types())
    w->supported_cmds.insert(cmd_attrs.get<std::uint32_t>(attr_type).value());

  auto iftype_attrs =
      msg_attrs.get<Attributes>(NL80211_ATTR_SUPPORTED_IFTYPES).value();
  for (auto attr_type : iftype_attrs.types())
    w->supported_iftypes.insert(attr_type);

  auto ciphers =
      msg_attrs.get<std::vector<std::uint32_t>>(NL80211_ATTR_CIPHER_SUITES)
          .value();
  for (auto c : ciphers) w->supported_ciphers.insert(c);

  auto bands = msg_attrs.get<Attributes>(NL80211_ATTR_WIPHY_BANDS).value();
  for (auto id : bands.types()) {
    struct band b = {};

    auto band_attrs = bands.get<Attributes>(id).value();
    auto freqs = band_attrs.get<Attributes>(NL80211_BAND_ATTR_FREQS).value();
    for (auto id : freqs.types()) {
      auto freq_attrs = freqs.get<Attributes>(id).value();
      auto freq_value =
          freq_attrs.get<std::uint32_t>(NL80211_FREQUENCY_ATTR_FREQ).value();
      b.freqs.insert(freq_value);
    }

    auto rates = band_attrs.get<Attributes>(NL80211_BAND_ATTR_RATES).value();
    for (auto id : rates.types()) {
      auto rate_attrs = rates.get<Attributes>(id).value();
      auto rate_value =
          rate_attrs.get<std::uint32_t>(NL80211_BITRATE_ATTR_RATE).value();
      b.bitrates.insert(0.1f * rate_value);
    }

    w->bands.push_back(b);
  }

  return true;
}

bool parse_interface_message(Attributes& msg_attrs, void* arg) {
  auto w = static_cast<struct wiface*>(arg);

  w->index = msg_attrs.get<std::uint32_t>(NL80211_ATTR_IFINDEX).value();
  w->wiphy = msg_attrs.get<std::uint32_t>(NL80211_ATTR_WIPHY).value();
  try {
    w->name = msg_attrs.get<std::string>(NL80211_ATTR_IFNAME).value();
  } catch (...) {
    w->name = "unnamed";
  }
  w->type = msg_attrs.get<std::uint32_t>(NL80211_ATTR_IFTYPE).value();

  auto mac = msg_attrs.get<std::vector<std::uint8_t>>(NL80211_ATTR_MAC).value();
  std::copy_n(mac.begin(), 6, w->mac.begin());

  return true;
}
}  // namespace

void new_key(Socket& nlsock, std::uint32_t if_idx, std::uint8_t key_idx,
             std::uint32_t cipher, std::array<std::uint8_t, 6> const& mac,
             std::vector<std::uint8_t> const& key) {
  Message msg(NL80211_CMD_NEW_KEY, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);
  msg.put(NL80211_ATTR_KEY_DATA, key);
  msg.put(NL80211_ATTR_KEY_CIPHER, cipher);
  msg.put(NL80211_ATTR_MAC, std::vector<std::uint8_t>(mac.begin(), mac.end()));
  msg.put(NL80211_ATTR_KEY_TYPE,
          static_cast<std::uint32_t>(NL80211_KEYTYPE_PAIRWISE));
  msg.put(NL80211_ATTR_KEY_IDX, key_idx);
  nlsock.send_message(msg);
  nlsock.recv_messages();
}

void del_key(Socket& nlsock, std::uint32_t if_idx, std::uint8_t key_idx,
             std::array<std::uint8_t, 6> const& mac) {
  Message msg(NL80211_CMD_DEL_KEY, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);
  msg.put(NL80211_ATTR_MAC, std::vector<std::uint8_t>(mac.begin(), mac.end()));
  msg.put(NL80211_ATTR_KEY_IDX, key_idx);
  nlsock.send_message(msg);
  nlsock.recv_messages();
}

void set_interface_mode(Socket& nlsock, std::uint32_t if_idx,
                        nl80211_iftype mode) {
  Message msg(NL80211_CMD_SET_INTERFACE, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);
  msg.put(NL80211_ATTR_IFTYPE, static_cast<std::uint32_t>(mode));
  nlsock.send_message(msg);
  nlsock.recv_messages();
}

void register_frame(Socket& nlsock, std::uint32_t if_idx, std::uint16_t type,
                    std::vector<std::uint8_t> const& match) {
  Message msg(NL80211_CMD_REGISTER_FRAME, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);
  msg.put(NL80211_ATTR_FRAME_TYPE, type);
  msg.put(NL80211_ATTR_FRAME_MATCH, match);
  nlsock.send_message(msg);
  nlsock.recv_messages();
}

void send_frame(Socket& nlsock, std::uint32_t if_idx, std::uint32_t freq,
                std::vector<uint8_t> const& data, std::uint32_t duration,
                bool wait_ack) {
  Message msg(NL80211_CMD_FRAME, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);
  msg.put(NL80211_ATTR_WIPHY_FREQ, freq);
  msg.put(NL80211_ATTR_FRAME, data);
  if (duration != 0) msg.put(NL80211_ATTR_DURATION, duration);
  if (!wait_ack) msg.put(NL80211_ATTR_DONT_WAIT_FOR_ACK);

  nlsock.send_message(msg);
  nlsock.recv_messages();
}

void join_ibss(Socket& nlsock, std::uint32_t if_idx, std::string const& ssid,
               std::uint32_t freq, bool fixed_freq,
               std::array<std::uint8_t, 6> const& bssid) {
  if (ssid.size() > 0x20) throw std::invalid_argument("SSID is too long");

  Message msg(NL80211_CMD_JOIN_IBSS, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);
  msg.put(NL80211_ATTR_SSID,
          std::vector<std::uint8_t>(ssid.begin(), ssid.end()));
  msg.put(NL80211_ATTR_WIPHY_FREQ, freq);
  msg.put(NL80211_ATTR_MAC,
          std::vector<std::uint8_t>(bssid.begin(), bssid.end()));
  if (fixed_freq) msg.put(NL80211_ATTR_FREQ_FIXED);

  nlsock.send_message(msg);
  nlsock.recv_messages();
}

wiface new_interface(Socket& nlsock, std::uint32_t wiphy, nl80211_iftype type,
                     std::string const& name, bool socket_owner) {
  if (name.size() > IFNAMSIZ - 1)
    throw std::invalid_argument("Interface name is too long");

  Message msg(NL80211_CMD_NEW_INTERFACE, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_WIPHY, wiphy);
  msg.put(NL80211_ATTR_IFTYPE, static_cast<std::uint32_t>(type));
  msg.put(NL80211_ATTR_IFNAME, name);
  if (socket_owner) msg.put(NL80211_ATTR_SOCKET_OWNER);
  nlsock.send_message(msg);

  struct wiface w;
  nlsock.recv_messages(parse_interface_message, &w);

  return w;
}

void del_interface(Socket& nlsock, std::uint32_t if_idx) {
  Message msg(NL80211_CMD_DEL_INTERFACE, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);

  nlsock.send_message(msg);
  nlsock.recv_messages();
}

wiphy get_wiphy(Socket& nlsock, std::uint32_t wiphy) {
  Message msg(NL80211_CMD_GET_WIPHY, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_WIPHY, wiphy);

  nlsock.send_message(msg);

  struct wiphy w = {};
  nlsock.recv_messages(parse_wiphy_message, &w);

  return w;
}

std::vector<wiphy> get_wiphy_list(Socket& nlsock) {
  auto resp_handler = [](Attributes& msg_attrs, void* arg) {
    auto v = static_cast<std::vector<struct wiphy>*>(arg);

    try {
      auto index = msg_attrs.get<std::uint32_t>(NL80211_ATTR_WIPHY).value();
      if (!v->empty() && v->back().index == index) return true;
    } catch (...) {
      return true;
    }

    struct wiphy w;
    parse_wiphy_message(msg_attrs, &w);

    v->push_back(w);
    return true;
  };
  Message msg(NL80211_CMD_GET_WIPHY, nlsock.get_driver_id(), NLM_F_DUMP);

  nlsock.send_message(msg);

  std::vector<struct wiphy> v;
  nlsock.recv_messages(resp_handler, &v);

  return v;
}

wiface get_interface(Socket& nlsock, std::uint32_t if_idx) {
  Message msg(NL80211_CMD_GET_INTERFACE, nlsock.get_driver_id());
  msg.put(NL80211_ATTR_IFINDEX, if_idx);

  nlsock.send_message(msg);

  struct wiface w = {};
  nlsock.recv_messages(parse_interface_message, &w);

  return w;
}

std::vector<wiface> get_interface_list(Socket& nlsock, std::uint32_t wiphy) {
  auto resp_handler = [](Attributes& msg_attrs, void* arg) {
    auto v = static_cast<std::vector<struct wiface>*>(arg);

    try {
      auto index = msg_attrs.get<std::uint32_t>(NL80211_ATTR_IFINDEX).value();
      if (!v->empty() && v->back().index == index) return true;
    } catch (...) {
      return true;
    }

    struct wiface i;
    parse_interface_message(msg_attrs, &i);
    v->push_back(i);

    return true;
  };
  Message msg(NL80211_CMD_GET_INTERFACE, nlsock.get_driver_id(), NLM_F_DUMP);
  msg.put(NL80211_ATTR_WIPHY, wiphy);

  nlsock.send_message(msg);

  std::vector<struct wiface> v;
  nlsock.recv_messages(resp_handler, &v);

  return v;
}
}  // namespace streetpass::nl80211::commands
