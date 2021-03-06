#include "iface/streetpass.hpp"

#include <tins/tins.h>

#include <chrono>
#include <thread>

#include "nl80211/message.hpp"

namespace streetpass::iface {

const std::string StreetpassInterface::SSID("Nintendo_3DS_continuous_scan_000");
const Tins::HWAddress<3> StreetpassInterface::OUI("00:1f:32");
const Tins::Dot11ManagementFrame::rates_type
    StreetpassInterface::SUPPORTED_RATES({1.0, 2.0, 5.5, 6.0, 9.0, 11.0, 12.0,
                                          18.0});
const Tins::Dot11ManagementFrame::rates_type
    StreetpassInterface::EXT_SUPPORTED_RATES({24.0, 36.0, 48.0, 54.0});
const int StreetpassInterface::CHANNEL_FREQ = 2412;

StreetpassInterface::StreetpassInterface(PhysicalInterface const& phys,
                                         std::string const& name) {
  // TODO: handle exception
  nl80211::wiface w = nl80211::commands::new_interface(
      nlsock, phys.get_id(), NL80211_IFTYPE_ADHOC, name, true);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  m_index = w.index;

  w = nl80211::commands::get_interface(nlsock, m_index);
  if (w.type != NL80211_IFTYPE_ADHOC) {
    down();
    nl80211::commands::set_interface_mode(nlsock, m_index,
                                          NL80211_IFTYPE_ADHOC);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  if (!is_up()) up();

  nl80211::commands::join_ibss(nlsock, m_index, SSID, CHANNEL_FREQ, true,
                               w.mac);
}

namespace {
bool is_streetpass_scan_probereq(Tins::Dot11ProbeRequest const& probereq) {
  try {
    return probereq.vendor_specific().oui == StreetpassInterface::OUI &&
           probereq.addr1().is_broadcast() &&
           probereq.ssid() == StreetpassInterface::SSID;
  } catch (Tins::option_not_found&) {
    return false;
  }
}
}  // namespace

void StreetpassInterface::scan_with_cb(
    unsigned int timeout,
    std::function<bool(Tins::HWAddress<6> const&,
                       cec::ModuleFilter const&)> const& callback) {
  // TODO: handle exception
  nl80211::Socket scan_sock;
  nl80211::commands::register_frame(
      scan_sock, m_index,
      Tins::Dot11::Types::MANAGEMENT |
          (Tins::Dot11::ManagementSubtypes::PROBE_REQ << 4));

  auto handler = [callback](nl80211::Attributes& msg_attrs, void*) {
    std::vector<std::uint8_t> data;
    try {
      data =
          msg_attrs.get<std::vector<std::uint8_t>>(NL80211_ATTR_FRAME).value();
    } catch (...) {
      return true;
    }

    Tins::Dot11ProbeRequest probereq(data.data(), data.size());
    if (!is_streetpass_scan_probereq(probereq)) return true;

    auto vendor_specific_data = probereq.vendor_specific().data;

    // TODO: first byte of vendor specific data is always 0x01?
    if (vendor_specific_data.size() == 0 || vendor_specific_data.at(0) != 0x01)
      return true;

    auto module_filter_bytes = &vendor_specific_data[1];
    unsigned module_filter_bytes_size = vendor_specific_data.size() - 1;
    try {
      auto peer_addr = probereq.addr2();
      cec::ModuleFilter module_filter =
          cec::Parser<cec::ModuleFilter>::from_bytes(module_filter_bytes,
                                                     module_filter_bytes_size);
      return callback(peer_addr, module_filter);
    } catch (...) {
      return true;
    }

    return true;
  };

  scan_sock.recv_messages(handler, nullptr, true, timeout);
}

std::map<Tins::HWAddress<6>, cec::ModuleFilter> StreetpassInterface::scan(
    unsigned int timeout,
    std::function<bool(Tins::HWAddress<6> const&,
                       cec::ModuleFilter const&)> const& filter) {
  std::map<Tins::HWAddress<6>, cec::ModuleFilter> results;
  if (timeout == 0) return results;

  auto callback = [&results, filter](Tins::HWAddress<6> const& peer_addr,
                                     cec::ModuleFilter const& module_filter) {
    if (filter(peer_addr, module_filter))
      results.emplace(peer_addr, module_filter);
    return true;
  };

  scan_with_cb(timeout, callback);
  return results;
}

std::map<Tins::HWAddress<6>, cec::ModuleFilter> StreetpassInterface::scan(
    unsigned int timeout) {
  auto no_filter = [](Tins::HWAddress<6> const&, cec::ModuleFilter const&) {
    return true;
  };

  return scan(timeout, no_filter);
}

std::map<Tins::HWAddress<6>, cec::ModuleFilter> StreetpassInterface::scan(
    unsigned int timeout, cec::ModuleFilter const& module_filter) {
  auto filter_match = [module_filter](Tins::HWAddress<6> const&,
                                      cec::ModuleFilter const& other) {
    return module_filter.match(other);
  };

  return scan(timeout, filter_match);
}

Tins::Dot11ProbeResponse StreetpassInterface::make_initial_proberesp(
    Tins::HWAddress<6> const& peer_addr,
    cec::ModuleFilter const& module_filter) {
  Tins::HWAddress<6> own_addr = get_mac_addr();
  Tins::Dot11ProbeResponse proberesp(peer_addr, own_addr);

  proberesp.ssid(SSID);
  // TODO: use real interface rates instead of hardcoded ones
  proberesp.supported_rates(SUPPORTED_RATES);
  proberesp.extended_supported_rates(EXT_SUPPORTED_RATES);
  proberesp.ds_parameter_set(1);
  proberesp.ibss_parameter_set(0);

  // TODO: maybe move the byte alias outside the cec namespace
  cec::bytes vendor_specific_data = cec::bytes(module_filter);
  // TODO: first byte is always 0x01?
  vendor_specific_data.insert(vendor_specific_data.begin(), 0x01);
  Tins::Dot11ManagementFrame::vendor_specific_type nintendo_vendor_ie(
      OUI, vendor_specific_data);

  proberesp.vendor_specific(nintendo_vendor_ie);

  return proberesp;
}

void StreetpassInterface::associate(unsigned int timeout,
                                    Tins::HWAddress<6> const& peer_addr,
                                    cec::ModuleFilter const& module_filter) {
  // TODO: check timeout nullity & throw exception
  bool found_peer = false;
  auto is_peer = [&found_peer, peer_addr, module_filter](
                     Tins::HWAddress<6> const& other_addr,
                     cec::ModuleFilter const& other) {
    if (other_addr == peer_addr /*&& module_filter.match(other)*/) {
      found_peer = true;
      return false;
    }
    return true;
  };

  scan(timeout, is_peer);
  if (!found_peer) {
    std::cout << "-- Could not find peer --" << std::endl;
    // TODO: throw exception
    return;
  }

  std::cout << "-- Found peer streetpass probe request --" << std::endl;
  // TODO: send initial probresp
  // TODO: wait for assoc probereq
  // TODO: send assoc proberesp
}

}  // namespace streetpass::iface
