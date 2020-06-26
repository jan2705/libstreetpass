#include "cec/module_filter.hpp"

#include <tins/endianness.h>
#include <iostream>
#include <iomanip>
using namespace Tins;
using Tins::Memory::InputMemoryStream;
using Tins::Memory::OutputMemoryStream;

namespace streetpass::cec {
  ModuleFilter::ModuleFilter(InputMemoryStream& stream) {
    parse(stream);
  }

  ModuleFilter::ModuleFilter(const uint8_t* buffer, uint32_t size) {
    InputMemoryStream stream(buffer, size);
    parse(stream);
  }

  ModuleFilter::ModuleFilter(bytes const& buffer) :
    ModuleFilter(buffer.data(), buffer.size()) {}

  bytes ModuleFilter::to_bytes() const {
    bytes buffer(m_title_list.total_size() + m_key_list.total_size());
    OutputMemoryStream stream(buffer);
    bytes tl_bytes = m_title_list.to_bytes();
    stream.write(tl_bytes.data(), tl_bytes.size());
    bytes cl_bytes = m_key_list.to_bytes();
    stream.write(cl_bytes.data(), cl_bytes.size());
    return buffer;
  }

  void ModuleFilter::parse(InputMemoryStream& stream) {
    bool found_title_list = false;
    bool found_key_list = false;

    while(stream.can_read(sizeof(filter_list_header))) {
      const filter_list_header* header =
        reinterpret_cast<const filter_list_header*>(stream.pointer());
      if(header->marker == filter_list_marker_t::TITLE_FILTER) {
        if(found_title_list)
          throw "bad - already found title list";
        m_title_list = FilterList<TitleFilter>(stream);
      } else if(header->marker == filter_list_marker_t::KEY_FILTER) {
        if(found_key_list)
          throw "bad - already found key list";
        m_key_list = FilterList<KeyFilter>(stream);
        if(m_key_list.count() != 1)
          throw "bad - key list count != 1";
      } else {
        std::cerr << m_title_list.count() << std::endl;
        std::cerr << std::hex << (int)header->marker<< std::endl;
        std::cerr << std::hex << (int)header->flags << std::endl;
        throw "bad marker";
      }
    }
  }

  std::ostream& operator<<(std::ostream& s, const ModuleFilter& e) {
    s << "================================ Module Filter =================================" << std::endl;
    s << e.m_title_list << std::endl;
    s << "--------------------------------------------------------------------------------" << std::endl;
    s << e.m_key_list << std::endl;
    s << "================================================================================";
    return s;
  }

  ModuleFilter::TitleFilter::TitleFilter(InputMemoryStream& stream) {
    parse(stream);
  }

  ModuleFilter::TitleFilter::TitleFilter(const uint8_t* buffer, uint32_t size) {
    InputMemoryStream stream(buffer, size);
    parse(stream);
  }

  ModuleFilter::TitleFilter::TitleFilter(bytes const& buffer) :
    TitleFilter(buffer.data(), buffer.size()) {}

  ModuleFilter::TitleFilter::TitleFilter(tid_type tid, send_mode_t mode) {
    title_id(tid);
    send_mode(mode);
  }

  void ModuleFilter::TitleFilter::parse(InputMemoryStream& stream) {
    if(!stream.can_read(sizeof(m_internal)))
      throw "bad title filter";
    stream.read(&m_internal, sizeof(m_internal));
    uint8_t mve_count = m_internal.number_mve;
    if(mve_count)
    {
      unsigned mve_byte_size = mve_count * sizeof(ModuleFilter::TitleFilter::title_filter_mve);
      if(!stream.can_read(mve_byte_size))
        throw "bad read mve";
      m_mve_list.resize(mve_count);
      stream.read(m_mve_list.data(), mve_byte_size);
    }
  }

  tid_type ModuleFilter::TitleFilter::title_id() const {
    return Endian::be_to_host(m_internal.title_id);
  }

  void ModuleFilter::TitleFilter::title_id(tid_type tid) {
    m_internal.title_id = Endian::host_to_be(tid);
  }

  send_mode_t ModuleFilter::TitleFilter::send_mode() const {
    return m_internal.send_mode;
  }

  void ModuleFilter::TitleFilter::send_mode(send_mode_t mode) {
    m_internal.send_mode = mode;
  }

  std::vector<ModuleFilter::TitleFilter::title_filter_mve> ModuleFilter::TitleFilter::mve_list() const {
    return m_mve_list;
  }

  void ModuleFilter::TitleFilter::mve_list(std::vector<ModuleFilter::TitleFilter::title_filter_mve> const& mve_list) {
    if(mve_list.size() > 0xF)
      throw std::length_error("MVE list size cannot exceed 15");

    m_mve_list = mve_list;
    m_internal.number_mve = mve_list.size();
  }

  unsigned ModuleFilter::TitleFilter::total_size() const {
    return sizeof(m_internal) + m_mve_list.size() * sizeof(ModuleFilter::TitleFilter::title_filter_mve);
  }

  bytes ModuleFilter::TitleFilter::to_bytes() const {
    bytes buffer(sizeof(m_internal) + m_mve_list.size() * sizeof(ModuleFilter::TitleFilter::title_filter_mve));
    OutputMemoryStream stream(buffer);
    stream.write(m_internal);

    auto mve_bytes = reinterpret_cast<const uint8_t*>(m_mve_list.data());
    stream.write(mve_bytes, m_mve_list.size() * sizeof(ModuleFilter::TitleFilter::title_filter_mve));
    return buffer;
  }

  std::ostream& operator<<(std::ostream& s, const ModuleFilter::TitleFilter& e) {
    std::stringstream ss;
    ss << "Title: ";
    ss <<"id=" << std::hex << std::setfill('0') << std::setw(8) << e.title_id()
      << ", send_mode=" << std::setw(2) << static_cast<unsigned>(e.send_mode());
    if(e.m_mve_list.size()) {
      ss << ", mve_list=";
      //TODO: better printer for MVE list
      for(ModuleFilter::TitleFilter::title_filter_mve const& mve: e.m_mve_list)
        ss << std::setw(2) << (int)mve.mask << " " << (int)mve.value << " " << (int)mve.expected;
    }

    s << ss.str();
    return s;
  }

  ModuleFilter::KeyFilter::KeyFilter(InputMemoryStream& stream) {
    parse(stream);
  }

  ModuleFilter::KeyFilter::KeyFilter(const uint8_t* buffer, uint32_t size) {
    InputMemoryStream stream(buffer, size);
    parse(stream);
  }

  ModuleFilter::KeyFilter::KeyFilter(bytes const& buffer) :
    KeyFilter(buffer.data(), buffer.size()) {}

  ModuleFilter::KeyFilter::KeyFilter(key_type const& k) {
    key(k);
  }

  void ModuleFilter::KeyFilter::parse(InputMemoryStream& stream) {
    if(!stream.can_read(sizeof(m_internal)))
      throw "bad key filter";
    stream.read(&m_internal, sizeof(m_internal));
  }

  key_type ModuleFilter::KeyFilter::key() const {
    key_type key;
    std::copy(std::begin(m_internal.key), std::end(m_internal.key), key.begin());
    return key;
  }

  void ModuleFilter::KeyFilter::key(key_type const& k) {
    std::memcpy(m_internal.key, k.data(), sizeof(m_internal.key));
  }

  unsigned ModuleFilter::KeyFilter::total_size() const {
    return sizeof(m_internal);
  }

  bytes ModuleFilter::KeyFilter::to_bytes() const {
    bytes buffer(sizeof(m_internal));
    OutputMemoryStream stream(buffer);
    stream.write(m_internal);
    return buffer;
  }

  std::ostream& operator<<(std::ostream& s, const ModuleFilter::KeyFilter& e) {
    std::stringstream ss;
    ss << "key=";
    ss << std::hex << std::setfill('0') << std::setw(2);
    for(unsigned b: e.m_internal.key)
      ss << std::setw(2) << b;

    s << ss.str();
    return s;
  }

  template<class T>
  ModuleFilter::FilterList<T>::FilterList(InputMemoryStream& stream) {
    parse(stream);
  }

  template<class T>
  ModuleFilter::FilterList<T>::FilterList(const uint8_t* buffer, uint32_t size) {
    InputMemoryStream stream(buffer, size);
    parse(stream);
  }

  template<class T>
  ModuleFilter::FilterList<T>::FilterList() : m_internal{} {
    marker(MARKER);
  }

  template<class T>
  ModuleFilter::FilterList<T>::FilterList(bytes const& buffer) :
    FilterList<T>(buffer.data(), buffer.size()) {}

  template<class T>
  void ModuleFilter::FilterList<T>::parse(InputMemoryStream& stream) {
    if(!stream.can_read(sizeof(m_internal)))
      throw "bad list header";
    stream.read(&m_internal, sizeof(m_internal));
    if(m_internal.marker != MARKER)
      throw "type mismatch";
    if(!stream.can_read(m_internal.length))
      throw "bad length";

    InputMemoryStream elt_steam(stream.pointer(), m_internal.length);
    while(elt_steam.size() > 0) {
      T t(elt_steam);
      m_list.push_back(t);
      stream.skip(t.total_size());
    }
  }

  template<class T>
  filter_list_marker_t ModuleFilter::FilterList<T>::marker() const {
    return m_internal.marker;
  }

  template<class T>
  void ModuleFilter::FilterList<T>::marker(filter_list_marker_t marker) {
    m_internal.marker = marker;
  }

  template<class T>
  small_uint<4> ModuleFilter::FilterList<T>::flags() const {
    return m_internal.flags;
  }

  template<class T>
  void ModuleFilter::FilterList<T>::flags(small_uint<4> flags) {
    m_internal.flags = flags;
  }

  template<class T>
  unsigned ModuleFilter::FilterList<T>::total_size() const {
    unsigned size = sizeof(m_internal);
    for(T const& e: m_list)
      size += e.total_size();
    return size;
  }

  template<class T>
  unsigned ModuleFilter::FilterList<T>::count() const {
    return m_list.size();
  }

  template<class T>
  bytes ModuleFilter::FilterList<T>::to_bytes() const {
    bytes buffer(sizeof(m_internal) + m_internal.length);
    OutputMemoryStream stream(buffer);
    stream.write(m_internal);
    for(T const& e: m_list) {
      bytes elt_bytes = e.to_bytes();
      stream.write(elt_bytes.data(), elt_bytes.size());
    }
    return buffer;
  }

  template<typename E>
  std::ostream& operator<<(std::ostream& s,
    const ModuleFilter::FilterList<E>& l)
  {
    std::stringstream ss;
    ss << "FilterList: ";
    ss << std::hex << std::setfill('0');
    ss << "marker=" << std::setw(2) << static_cast<unsigned>(l.marker()) << ", ";
    ss << "flags=" << std::setw(2) << static_cast<unsigned>(l.flags()) << ", ";
    ss << "length=" << std::setw(2) << static_cast<unsigned>(l.m_internal.length) << std::endl;
    ss << "Content:";
    for(auto const& e: l.m_list)
      ss << std::endl << e;
    s << ss.str();
    return s;
  }

  template<>
  const filter_list_marker_t ModuleFilter::FilterList<ModuleFilter::TitleFilter>::MARKER =
    filter_list_marker_t::TITLE_FILTER;
  template<>
  const filter_list_marker_t ModuleFilter::FilterList<ModuleFilter::KeyFilter>::MARKER =
    filter_list_marker_t::KEY_FILTER;

  template class ModuleFilter::FilterList<ModuleFilter::TitleFilter>;
  template class ModuleFilter::FilterList<ModuleFilter::KeyFilter>;
}
