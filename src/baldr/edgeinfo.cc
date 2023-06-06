#include "baldr/edgeinfo.h"
#include "baldr/graphconstants.h"

#include "midgard/encoded.h"

using namespace valhalla::baldr;

namespace {

// should return true for any tags which we should consider "named"
// do not return TaggedValue::kPronunciation or TaggedValue::kLanguage
bool IsNameTag(char ch) {
  static const std::unordered_set<TaggedValue> kNameTags = {TaggedValue::kBridge,
                                                            TaggedValue::kTunnel};
  return kNameTags.count(static_cast<TaggedValue>(static_cast<uint8_t>(ch))) > 0;
}

json::MapPtr bike_network_json(uint8_t mask) {
  return json::map({
      {"national", static_cast<bool>(mask & kNcn)},
      {"regional", static_cast<bool>(mask & kRcn)},
      {"local", static_cast<bool>(mask & kLcn)},
      {"mountain", static_cast<bool>(mask & kMcn)},
  });
}

json::ArrayPtr names_json(const std::vector<std::string>& names) {
  auto a = json::array({});
  for (const auto& n : names) {
    a->push_back(n);
  }
  return a;
}

} // namespace

namespace valhalla {
namespace baldr {

EdgeInfo::EdgeInfo(char* ptr, const char* names_list, const size_t names_list_length)
    : names_list_(names_list), names_list_length_(names_list_length) {

  ei_ = *reinterpret_cast<EdgeInfoInner*>(ptr);
  ptr += sizeof(EdgeInfoInner);

  // Set name info list pointer
  name_info_list_ = reinterpret_cast<NameInfo*>(ptr);
  ptr += (name_count() * sizeof(NameInfo));

  // Set encoded_shape_ pointer
  encoded_shape_ = ptr;
  ptr += (encoded_shape_size() * sizeof(char));

  // Optional second half of 64bit way id
  extended_wayid2_ = extended_wayid3_ = 0;
  if (ei_.extended_wayid_size_ > 0) {
    extended_wayid2_ = static_cast<uint8_t>(*ptr);
    ptr += sizeof(uint8_t);
  }
  if (ei_.extended_wayid_size_ > 1) {
    extended_wayid3_ = static_cast<uint8_t>(*ptr);
    ptr += sizeof(uint8_t);
  }
}

EdgeInfo::~EdgeInfo() {
  // nothing to delete these are all shallow pointers for the moment held
  // by another object
}

// Get the name info for the specified name index.
NameInfo EdgeInfo::GetNameInfo(uint8_t index) const {
  if (index < ei_.name_count_) {
    return name_info_list_[index];
  } else {
    throw std::runtime_error("StreetNameOffset index was out of bounds");
  }
}

// Get a list of names
std::vector<std::string> EdgeInfo::GetNames() const {
  // Get each name
  std::vector<std::string> names;
  names.reserve(name_count());
  const NameInfo* ni = name_info_list_;
  for (uint32_t i = 0; i < name_count(); i++, ni++) {
    if (ni->tagged_)
      continue;

    if (ni->name_offset_ < names_list_length_) {
      names.push_back(names_list_ + ni->name_offset_);
    } else {
      throw std::runtime_error("GetNames: offset exceeds size of text list");
    }
  }
  return names;
}

// Get the non linguistic, tagged names for an edge
std::vector<std::string> EdgeInfo::GetTaggedValues() const {
  // Get each name
  std::vector<std::string> names;
  names.reserve(name_count());
  const NameInfo* ni = name_info_list_;
  for (uint32_t i = 0; i < name_count(); i++, ni++) {
    if (!ni->tagged_)
      continue;

    if (ni->name_offset_ < names_list_length_) {
      const auto* name = names_list_ + ni->name_offset_;
      try {
        TaggedValue tv = static_cast<baldr::TaggedValue>(name[0]);
        if (tv == baldr::TaggedValue::kPronunciation || tv == baldr::TaggedValue::kLanguage) {
          continue;
        } else {
          names.push_back(name);
        }
      } catch (const std::invalid_argument& arg) {
        LOG_DEBUG("invalid_argument thrown for name: " + std::string(name));
      }
    } else {
      throw std::runtime_error("GetTaggedNames: offset exceeds size of text list");
    }
  }
  return names;
}

// Get the linguistic, tagged names for an edge
std::vector<std::string> EdgeInfo::GetLinguisticTaggedValues(const baldr::TaggedValue type) const {
  // Get each name
  std::vector<std::string> names;

  if (type == baldr::TaggedValue::kPronunciation || type == baldr::TaggedValue::kLanguage) {
    names.reserve(name_count());
    const NameInfo* ni = name_info_list_;
    for (uint32_t i = 0; i < name_count(); i++, ni++) {
      if (!ni->tagged_)
        continue;

      if (ni->name_offset_ < names_list_length_) {
        const auto* name = names_list_ + ni->name_offset_;
        try {
          TaggedValue tv = static_cast<baldr::TaggedValue>(name[0]);
          if (tv == type) {
            name += 1;
            while (*name != '\0') {
              if (type == baldr::TaggedValue::kPronunciation) {
                const auto header = midgard::unaligned_read<linguistic_text_header_t>(name);
                names.emplace_back(
                    std::string(reinterpret_cast<const char*>(&header), kLinguisticHeaderSize) +
                    std::string((name + kLinguisticHeaderSize), header.length_));
                name += header.length_ + kLinguisticHeaderSize;

              } else if (type == baldr::TaggedValue::kLanguage) {
                const auto header = midgard::unaligned_read<language_text_header_t>(name);
                names.emplace_back(
                    std::string(reinterpret_cast<const char*>(&header), kLanguageHeaderSize));
                name += kLanguageHeaderSize;
              }
            }
          }
        } catch (const std::invalid_argument& arg) {
          LOG_DEBUG("invalid_argument thrown for name: " + std::string(name));
        }
      } else {
        throw std::runtime_error("GetTaggedNames: offset exceeds size of text list");
      }
    }
  }
  return names;
}

// Get a list of names
std::vector<std::pair<std::string, bool>>
EdgeInfo::GetNamesAndTypes(std::vector<uint8_t>& types, bool include_tagged_values) const {

  // Get each name
  std::vector<std::pair<std::string, bool>> name_type_pairs;
  name_type_pairs.reserve(name_count());
  const NameInfo* ni = name_info_list_;
  for (uint32_t i = 0; i < name_count(); i++, ni++) {
    // Skip any tagged names (FUTURE code may make use of them)
    if (ni->tagged_ && !include_tagged_values) {
      continue;
    }
    if (ni->tagged_) {
      if (ni->name_offset_ < names_list_length_) {
        std::string name = names_list_ + ni->name_offset_;
        try {
          if (IsNameTag(name[0])) {
            name_type_pairs.push_back({name.substr(1), false});
            types.push_back(static_cast<uint8_t>(name.at(0)));
          }
        } catch (const std::invalid_argument& arg) {
          LOG_DEBUG("invalid_argument thrown for name: " + name);
        }
      } else
        throw std::runtime_error("GetNamesAndTypes: offset exceeds size of text list");
    } else if (ni->name_offset_ < names_list_length_) {
      name_type_pairs.push_back({names_list_ + ni->name_offset_, ni->is_route_num_});
      types.push_back(0);
    } else {
      throw std::runtime_error("GetNamesAndTypes: offset exceeds size of text list");
    }
  }
  return name_type_pairs;
}

// Get a list of tagged names
const std::multimap<TaggedValue, std::string>& EdgeInfo::GetTags() const {
  // we could check `tag_cache_.empty()` here, but many edges contain no tags
  // and it would mean we traverse all names on each `GetTags` call
  // for such edges
  if (!tag_cache_ready_) {
    // Get each name
    const NameInfo* ni = name_info_list_;
    for (uint32_t i = 0; i < name_count(); i++, ni++) {
      // Skip any non tagged names
      if (ni->tagged_) {
        if (ni->name_offset_ < names_list_length_) {
          std::string name = names_list_ + ni->name_offset_;
          try {
            TaggedValue tv = static_cast<baldr::TaggedValue>(name[0]);
            if (tv != baldr::TaggedValue::kPronunciation && tv != baldr::TaggedValue::kLanguage)
              tag_cache_.emplace(tv, name.substr(1));
          } catch (const std::logic_error& arg) { LOG_DEBUG("logic_error thrown for name: " + name); }
        } else {
          throw std::runtime_error("GetTags: offset exceeds size of text list");
        }
      }
    }

    if (tag_cache_.size())
      tag_cache_ready_ = true;
  }

  return tag_cache_;
}

std::unordered_map<uint8_t, std::tuple<uint8_t, uint8_t, std::string>>
EdgeInfo::GetLinguisticMap() const {
  std::unordered_map<uint8_t, std::tuple<uint8_t, uint8_t, std::string>> index_linguistic_map;
  index_linguistic_map.reserve(name_count());
  const NameInfo* ni = name_info_list_;
  for (uint32_t i = 0; i < name_count(); i++, ni++) {
    if (!ni->tagged_)
      continue;

    if (ni->name_offset_ < names_list_length_) {
      const auto* name = names_list_ + ni->name_offset_;
      try {
        TaggedValue tv = static_cast<baldr::TaggedValue>(name[0]);
        if (tv == baldr::TaggedValue::kPronunciation || tv == baldr::TaggedValue::kLanguage) {
          name += 1;
          while (*name != '\0') {
            std::tuple<uint8_t, uint8_t, std::string> liguistic_attributes;
            uint8_t name_index = 0;
            if (tv == baldr::TaggedValue::kPronunciation) {
              const auto header = midgard::unaligned_read<linguistic_text_header_t>(name);

              std::get<kLinguisticMapTuplePhoneticAlphabetIndex>(liguistic_attributes) =
                  header.phonetic_alphabet_;
              std::get<kLinguisticMapTupleLanguageIndex>(liguistic_attributes) = header.language_;

              std::get<kLinguisticMapTuplePronunciationIndex>(liguistic_attributes) =
                  std::string(name + kLinguisticHeaderSize, header.length_);
              name += header.length_ + kLinguisticHeaderSize;
              name_index = header.name_index_;

            } else if (tv == baldr::TaggedValue::kLanguage) {
              const auto header = midgard::unaligned_read<language_text_header_t>(name);

              std::get<kLinguisticMapTuplePhoneticAlphabetIndex>(liguistic_attributes) =
                  static_cast<uint8_t>(PronunciationAlphabet::kNone);
              std::get<kLinguisticMapTupleLanguageIndex>(liguistic_attributes) = header.language_;
              std::get<kLinguisticMapTuplePronunciationIndex>(liguistic_attributes) = "";

              name += kLanguageHeaderSize;
              name_index = header.name_index_;
            } else
              continue;

            auto iter = index_linguistic_map.insert(std::make_pair(name_index, liguistic_attributes));

            if (!iter.second) {
              if ((std::get<kLinguisticMapTuplePhoneticAlphabetIndex>(liguistic_attributes) >
                   std::get<kLinguisticMapTuplePhoneticAlphabetIndex>(iter.first->second)) &&
                  (std::get<kLinguisticMapTupleLanguageIndex>(liguistic_attributes) ==
                   std::get<kLinguisticMapTupleLanguageIndex>(iter.first->second))) {
                iter.first->second = liguistic_attributes;
              }
            }
          }
        }
      } catch (const std::invalid_argument& arg) {
        LOG_DEBUG("invalid_argument thrown for name: " + std::string(name));
      }
    } else {
      throw std::runtime_error("GetLinguisticMap: offset exceeds size of text list");
    }
  }

  return index_linguistic_map;
}

// Get the types.  Are these names route numbers or not?
uint16_t EdgeInfo::GetTypes() const {
  // Get the types.
  uint16_t types = 0;
  for (uint32_t i = 0; i < name_count(); i++) {
    NameInfo info = GetNameInfo(i);
    types |= static_cast<uint64_t>(info.is_route_num_) << i;
  }
  return types;
}

// Returns shape as a vector of PointLL
// TODO: use shared ptr here so that we dont have to worry about lifetime
const std::vector<midgard::PointLL>& EdgeInfo::shape() const {
  // if we haven't yet decoded the shape, do so
  if (encoded_shape_ != nullptr && shape_.empty()) {
    shape_ = midgard::decode7<std::vector<midgard::PointLL>>(encoded_shape_, ei_.encoded_shape_size_);
  }
  return shape_;
}

// Returns the encoded shape string
std::string EdgeInfo::encoded_shape() const {
  return encoded_shape_ == nullptr ? midgard::encode7(shape_)
                                   : std::string(encoded_shape_, ei_.encoded_shape_size_);
}

int8_t EdgeInfo::layer() const {
  const auto& tags = GetTags();
  auto itr = tags.find(TaggedValue::kLayer);
  if (itr == tags.end()) {
    return 0;
  }
  const auto& value = itr->second;
  if (value.size() != 1) {
    throw std::runtime_error("layer must contain 1-byte value");
  }
  return static_cast<int8_t>(value.front());
}

std::string EdgeInfo::level() const {
  const auto& tags = GetTags();
  auto itr = tags.find(TaggedValue::kLevel);
  if (itr == tags.end()) {
    return "";
  }
  const std::string& value = itr->second;
  return value;
}

std::string EdgeInfo::level_ref() const {
  const auto& tags = GetTags();
  auto itr = tags.find(TaggedValue::kLevelRef);
  if (itr == tags.end()) {
    return "";
  }
  const std::string& value = itr->second;
  return value;
}

json::MapPtr EdgeInfo::json() const {
  json::MapPtr edge_info = json::map({
      {"way_id", static_cast<uint64_t>(wayid())},
      {"bike_network", bike_network_json(bike_network())},
      {"names", names_json(GetNames())},
      {"shape", midgard::encode(shape())},
  });
  // add the mean_elevation depending on its validity
  const auto elev = mean_elevation();
  if (elev == kNoElevationData) {
    edge_info->emplace("mean_elevation", nullptr);
  } else {
    edge_info->emplace("mean_elevation", static_cast<int64_t>(elev));
  }

  if (speed_limit() == kUnlimitedSpeedLimit) {
    edge_info->emplace("speed_limit", std::string("unlimited"));
  } else {
    edge_info->emplace("speed_limit", static_cast<uint64_t>(speed_limit()));
  }

  return edge_info;
}

} // namespace baldr
} // namespace valhalla
