#include "harmony_protocol.h"

#include <algorithm>
#include <cstdio>

#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#else
#define ESP_LOGVV(tag, format, ...) ((void) 0)
#endif

namespace esphome::harmoino {

static const char *const TAG = "harmoino";

const std::vector<HarmonyCommandDefinition> &default_harmony_commands() {
  static const std::vector<HarmonyCommandDefinition> kCommands = {
#define HARMONY_COMMAND(id, type, name) {id, type, name},
#include "default_harmony_commands.h"
#undef HARMONY_COMMAND
  };
  return kCommands;
}

std::string format_hex_value(uint64_t value, uint8_t width) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%0*llX", width, static_cast<unsigned long long>(value));
  return std::string(buffer);
}

std::string format_address_text(std::optional<uint64_t> address) {
  if (!address.has_value()) {
    return "";
  }
  return "0x" + format_hex_value(*address, 10);
}

std::string format_hex_bytes(const uint8_t *data, size_t length) {
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

  std::string formatted;
  if (length == 0 || data == nullptr) {
    return formatted;
  }

  formatted.reserve((length * 3U) - 1U);
  for (size_t index = 0; index < length; index++) {
    if (index > 0) {
      formatted.push_back(' ');
    }
    const uint8_t value = data[index];
    formatted.push_back(HEX_DIGITS[(value >> 4U) & 0x0FU]);
    formatted.push_back(HEX_DIGITS[value & 0x0FU]);
  }
  return formatted;
}

void HarmonyCommandResolver::add_override(uint32_t id, const std::string &name, uint8_t type) {
  auto existing = std::find_if(this->overrides_.begin(), this->overrides_.end(),
                               [id](const HarmonyCommandOverride &entry) { return entry.id == id; });
  if (existing != this->overrides_.end()) {
    existing->name = name;
    existing->type = type;
    return;
  }
  this->overrides_.push_back({id, type, name});
}

const HarmonyCommandDefinition *HarmonyCommandResolver::find_default_(uint32_t id) const {
  for (const auto &command : default_harmony_commands()) {
    if (command.id == id) {
      return &command;
    }
  }
  return nullptr;
}

const HarmonyCommandOverride *HarmonyCommandResolver::find_override_(uint32_t id) const {
  auto it = std::find_if(this->overrides_.begin(), this->overrides_.end(),
                         [id](const HarmonyCommandOverride &entry) { return entry.id == id; });
  if (it == this->overrides_.end()) {
    return nullptr;
  }
  return &(*it);
}

HarmonyResolvedCommand HarmonyCommandResolver::resolve(uint32_t id) const {
  if (const auto *override_entry = this->find_override_(id)) {
    return {id, override_entry->type, override_entry->name, true};
  }
  if (const auto *default_entry = this->find_default_(id)) {
    return {id, default_entry->type, default_entry->name, true};
  }
  return {id, 0, format_hex_value(id, 8), false};
}

void HarmonyCommandResolver::append_payload_types_(std::vector<std::string> &event_types, const std::string &name,
                                                   uint8_t type) {
  const auto append_unique = [&event_types](const std::string &value) {
    if (std::find(event_types.begin(), event_types.end(), value) == event_types.end()) {
      event_types.push_back(value);
    }
  };

  if (type <= 1) {
    append_unique(name);
    return;
  }

  append_unique(name + "_clicked");
  append_unique(name + "_double");
  append_unique(name + "_multiple");
  append_unique(name + "_long");
}

std::vector<std::string> HarmonyCommandResolver::build_event_types() const {
  std::vector<std::string> event_types;
  for (const auto &default_entry : default_harmony_commands()) {
    const auto resolved = this->resolve(default_entry.id);
    append_payload_types_(event_types, resolved.name, resolved.type);
  }
  for (const auto &override_entry : this->overrides_) {
    if (this->find_default_(override_entry.id) != nullptr) {
      continue;
    }
    append_payload_types_(event_types, override_entry.name, override_entry.type);
  }
  if (std::find(event_types.begin(), event_types.end(), "sleep") == event_types.end()) {
    event_types.push_back("sleep");
  }
  return event_types;
}

void HarmonyDecoder::set_timings(uint32_t click_duration_ms, uint32_t wait_duration_ms,
                                 uint32_t second_repeat_duration_ms, uint32_t further_repeat_duration_ms,
                                 uint32_t sleep_timeout_ms) {
  this->click_duration_ms_ = click_duration_ms;
  this->wait_duration_ms_ = wait_duration_ms;
  this->second_repeat_duration_ms_ = second_repeat_duration_ms;
  this->further_repeat_duration_ms_ = further_repeat_duration_ms;
  this->sleep_timeout_ms_ = sleep_timeout_ms;
}

HarmonyResolvedCommand HarmonyDecoder::resolve_(uint32_t command_id) const {
  if (this->resolver_ != nullptr) {
    return this->resolver_->resolve(command_id);
  }
  HarmonyCommandResolver resolver;
  return resolver.resolve(command_id);
}

void HarmonyDecoder::begin_sequence_(const HarmonyResolvedCommand &command, uint32_t now_ms) {
  this->sequence_active_ = true;
  this->pending_release_ = false;
  this->release_emitted_ = false;
  this->current_press_hold_seen_ = false;
  this->long_emitted_ = false;
  this->initial_emitted_ = false;
  this->repeat_stage_ = 0;
  this->click_count_ = 1;
  this->current_press_start_ms_ = now_ms;
  this->last_press_ms_ = now_ms;
  this->last_release_ms_ = 0;
  this->last_hold_ms_ = 0;
  this->last_related_ms_ = now_ms;
  this->next_repeat_ms_ = 0;
  this->current_command_ = command;
}

void HarmonyDecoder::clear_sequence_() {
  this->sequence_active_ = false;
  this->pending_release_ = false;
  this->release_emitted_ = false;
  this->current_press_hold_seen_ = false;
  this->long_emitted_ = false;
  this->initial_emitted_ = false;
  this->repeat_stage_ = 0;
  this->click_count_ = 0;
  this->current_press_start_ms_ = 0;
  this->last_press_ms_ = 0;
  this->last_release_ms_ = 0;
  this->last_hold_ms_ = 0;
  this->last_related_ms_ = 0;
  this->next_repeat_ms_ = 0;
  this->current_command_ = {};
}

std::vector<HarmonyOutput> HarmonyDecoder::finalize_click_sequence_() {
  std::vector<HarmonyOutput> outputs;
  if (!this->sequence_active_ || this->current_command_.type != 2 || this->long_emitted_ || this->click_count_ == 0) {
    this->clear_sequence_();
    return outputs;
  }

  std::string payload;
  if (this->click_count_ == 1) {
    payload = this->current_command_.name + "_clicked";
  } else if (this->click_count_ == 2) {
    payload = this->current_command_.name + "_double";
  } else {
    payload = this->current_command_.name + "_multiple";
  }
  outputs.push_back({this->current_command_.id, payload, this->current_command_.known});
  this->clear_sequence_();
  return outputs;
}

bool HarmonyDecoder::should_treat_press_as_hold_continuation_(uint32_t now_ms) const {
  if (!this->sequence_active_ || !this->pending_release_) {
    return false;
  }
  if ((now_ms - this->last_release_ms_) > RELEASE_CHATTER_MAX_GAP_MS) {
    return false;
  }
  if (this->current_press_hold_seen_) {
    return true;
  }
  if (this->last_hold_ms_ != 0 && (now_ms - this->last_hold_ms_) <= this->wait_duration_ms_) {
    return true;
  }
  if (this->current_command_.type != 2) {
    return false;
  }
  if ((now_ms - this->current_press_start_ms_) >= this->click_duration_ms_) {
    return true;
  }
  return false;
}

bool HarmonyDecoder::current_press_active_(uint32_t now_ms) const {
  return this->sequence_active_ && !this->pending_release_ && (now_ms - this->last_related_ms_) <= this->wait_duration_ms_;
}

std::vector<HarmonyOutput> HarmonyDecoder::handle_press_(uint32_t command_id, uint32_t now_ms) {
  std::vector<HarmonyOutput> outputs;
  const auto resolved = this->resolve_(command_id);

  this->awake_ = true;
  this->last_activity_ms_ = now_ms;

  if (!this->sequence_active_) {
    this->begin_sequence_(resolved, now_ms);
    outputs.push_back({resolved.id, resolved.name, resolved.known, HarmonyOutputKind::PRESS});
    return outputs;
  }

  if (command_id != this->current_command_.id) {
    if (this->pending_release_) {
      auto completed = this->finalize_click_sequence_();
      outputs.insert(outputs.end(), completed.begin(), completed.end());
    } else {
      this->clear_sequence_();
    }
    this->begin_sequence_(resolved, now_ms);
    outputs.push_back({resolved.id, resolved.name, resolved.known, HarmonyOutputKind::PRESS});
    return outputs;
  }

  if (this->pending_release_) {
    if (this->should_treat_press_as_hold_continuation_(now_ms)) {
      this->pending_release_ = false;
      this->last_press_ms_ = now_ms;
      this->last_related_ms_ = now_ms;
      return outputs;
    }

    this->pending_release_ = false;
    this->release_emitted_ = false;
    this->current_press_hold_seen_ = false;
    this->last_press_ms_ = now_ms;
    this->last_related_ms_ = now_ms;
    this->current_press_start_ms_ = now_ms;
    this->click_count_++;
    this->initial_emitted_ = false;
    this->repeat_stage_ = 0;
    this->next_repeat_ms_ = 0;
    outputs.push_back({resolved.id, resolved.name, resolved.known, HarmonyOutputKind::PRESS});
    return outputs;
  }

  if ((now_ms - this->last_press_ms_) <= 30U) {
    return outputs;
  }

  this->last_press_ms_ = now_ms;
  this->last_related_ms_ = now_ms;
  return outputs;
}

std::vector<HarmonyOutput> HarmonyDecoder::handle_release_(uint32_t command_id, uint32_t now_ms) {
  this->awake_ = true;
  this->last_activity_ms_ = now_ms;

  if (!this->sequence_active_ || command_id != (this->current_command_.id & 0x000000FFU)) {
    return {};
  }

  this->pending_release_ = true;
  this->release_emitted_ = false;
  this->last_release_ms_ = now_ms;
  this->last_related_ms_ = now_ms;
  return {};
}

std::vector<HarmonyOutput> HarmonyDecoder::handle_hold_(uint32_t now_ms) {
  this->awake_ = true;
  this->last_activity_ms_ = now_ms;

  if (!this->sequence_active_) {
    return {};
  }

  this->pending_release_ = false;
  this->current_press_hold_seen_ = true;
  this->last_hold_ms_ = now_ms;
  this->last_related_ms_ = now_ms;
  return {};
}

std::vector<HarmonyOutput> HarmonyDecoder::handle_ping_(uint32_t now_ms) {
  this->awake_ = true;
  this->ping_observed_ = true;
  this->last_activity_ms_ = now_ms;
  return {{HARMONY_PING, "ping", true, HarmonyOutputKind::RAW_EVENT}};
}

std::vector<HarmonyOutput> HarmonyDecoder::handle_sleep_(uint32_t now_ms) {
  this->last_activity_ms_ = now_ms;
  this->ping_observed_ = false;
  this->awake_ = false;
  this->clear_sequence_();
  return {{HARMONY_SLEEP, "sleep", true}};
}

std::vector<HarmonyOutput> HarmonyDecoder::process_packet(const std::vector<uint8_t> &packet, uint32_t now_ms) {
  const auto info = classify_harmony_packet(packet, this->resolver_);
  switch (info.kind) {
    case HarmonyPacketKind::PRESS:
      return this->handle_press_(info.command_id, now_ms);
    case HarmonyPacketKind::RELEASE:
      return this->handle_release_(info.command_id, now_ms);
    case HarmonyPacketKind::HOLD:
      return this->handle_hold_(now_ms);
    case HarmonyPacketKind::PING:
      return this->handle_ping_(now_ms);
    case HarmonyPacketKind::SLEEP:
      return this->handle_sleep_(now_ms);
    case HarmonyPacketKind::STATE:
    case HarmonyPacketKind::UNKNOWN:
    default:
      return {};
  }
}

std::vector<HarmonyOutput> HarmonyDecoder::process_command_id(uint32_t command_id, uint32_t now_ms) {
  if (command_id == HARMONY_HOLD) {
    return this->handle_hold_(now_ms);
  }
  if (command_id == HARMONY_PING) {
    return this->handle_ping_(now_ms);
  }
  if (command_id == HARMONY_SLEEP) {
    return this->handle_sleep_(now_ms);
  }
  if (command_id == 0x000000C1U || command_id == 0x000000C3U) {
    return this->handle_release_(command_id, now_ms);
  }
  if (((command_id & 0xFF000000U) != 0U) && this->resolver_ != nullptr) {
    const auto related = this->resolve_(command_id & 0x00FFFFFFU);
    if (related.known) {
      ESP_LOGVV(TAG, "Ignoring Harmony state packet 0x%08X linked to 0x%08X (%s)", command_id, related.id,
                related.name.c_str());
      return {};
    }
  }
  if ((command_id & 0x000000F0U) == 0x000000C0U) {
    return this->handle_press_(command_id, now_ms);
  }
  return {};
}

std::vector<HarmonyOutput> HarmonyDecoder::poll(uint32_t now_ms) {
  std::vector<HarmonyOutput> outputs;

  if (this->sequence_active_) {
    if (this->pending_release_ && !this->release_emitted_ &&
        (now_ms - this->last_release_ms_) > RELEASE_CHATTER_MAX_GAP_MS) {
      outputs.push_back(
          {this->current_command_.id, this->current_command_.name, this->current_command_.known, HarmonyOutputKind::RELEASE});
      this->release_emitted_ = true;
    }

    if (this->current_command_.type <= 1) {
      if (!this->initial_emitted_) {
        outputs.push_back({this->current_command_.id, this->current_command_.name, this->current_command_.known});
        this->initial_emitted_ = true;
        if (this->current_command_.type == 1) {
          this->repeat_stage_ = 1;
          this->next_repeat_ms_ = now_ms + this->second_repeat_duration_ms_;
        }
      } else if (this->current_command_.type == 1 && this->current_press_active_(now_ms) && this->current_press_hold_seen_ &&
                 now_ms >= this->next_repeat_ms_) {
        outputs.push_back({this->current_command_.id, this->current_command_.name, this->current_command_.known});
        if (this->repeat_stage_ <= 1) {
          this->repeat_stage_ = 2;
          this->next_repeat_ms_ = now_ms + this->further_repeat_duration_ms_;
        } else {
          this->next_repeat_ms_ = now_ms + this->further_repeat_duration_ms_;
        }
      }

      if ((now_ms - this->last_related_ms_) > this->wait_duration_ms_) {
        this->clear_sequence_();
      }
    } else {
      if (!this->long_emitted_ && this->current_press_active_(now_ms) &&
          (now_ms - this->current_press_start_ms_) >= this->click_duration_ms_) {
        outputs.push_back({this->current_command_.id, this->current_command_.name + "_long", this->current_command_.known});
        this->long_emitted_ = true;
      }

      if (!this->long_emitted_ && this->pending_release_ &&
          (now_ms - this->last_release_ms_) > this->wait_duration_ms_) {
        auto completed = this->finalize_click_sequence_();
        outputs.insert(outputs.end(), completed.begin(), completed.end());
      } else if (this->long_emitted_ && (now_ms - this->last_related_ms_) > this->wait_duration_ms_) {
        this->clear_sequence_();
      }
    }
  }

  if (this->awake_ && this->ping_observed_ && (now_ms - this->last_activity_ms_) > this->sleep_timeout_ms_) {
    this->awake_ = false;
    this->ping_observed_ = false;
    outputs.push_back({HARMONY_SLEEP, "sleep", true});
  }

  return outputs;
}

void HarmonyDecoder::reset() {
  this->click_duration_ms_ = DEFAULT_CLICK_DURATION_MS;
  this->wait_duration_ms_ = DEFAULT_WAIT_DURATION_MS;
  this->second_repeat_duration_ms_ = DEFAULT_SECOND_REPEAT_DURATION_MS;
  this->further_repeat_duration_ms_ = DEFAULT_FURTHER_REPEAT_DURATION_MS;
  this->sleep_timeout_ms_ = DEFAULT_SLEEP_TIMEOUT_MS;
  this->awake_ = false;
  this->ping_observed_ = false;
  this->last_activity_ms_ = 0;
  this->clear_sequence_();
}

std::optional<uint32_t> extract_harmony_command_id(const std::vector<uint8_t> &packet) {
  if (packet.size() < 5) {
    return std::nullopt;
  }

  uint32_t command_id = 0;
  for (int index = 4; index >= 1; index--) {
    command_id <<= 8;
    command_id += static_cast<uint32_t>(packet[static_cast<size_t>(index)]);
  }
  return command_id;
}

std::optional<uint64_t> extract_remote_address(const std::vector<uint8_t> &payload) {
  if (payload.size() != 22) {
    return std::nullopt;
  }

  uint64_t address = 0;
  for (size_t index = 3; index <= 7; index++) {
    uint8_t value = payload[index];
    if (index == 7) {
      value = static_cast<uint8_t>(value - 1);
    }
    address <<= 8;
    address |= static_cast<uint64_t>(value);
  }
  return address;
}

std::optional<uint64_t> select_effective_address(std::optional<uint64_t> yaml_address,
                                                 std::optional<uint64_t> saved_address) {
  if (yaml_address.has_value()) {
    return yaml_address;
  }
  return saved_address;
}

HarmonyPacketInfo classify_harmony_packet(const std::vector<uint8_t> &packet, const HarmonyCommandResolver *resolver) {
  HarmonyPacketInfo info;
  const auto command_id = extract_harmony_command_id(packet);
  if (!command_id.has_value()) {
    return info;
  }

  info.command_id = *command_id;
  if (packet.size() == 5U && packet[0] == 0x00 && packet[1] == 0x40 && packet[2] == 0x00 && packet[3] == 0x28 &&
      packet[4] == 0x98) {
    info.kind = HarmonyPacketKind::HOLD;
    return info;
  }
  if (packet.size() == 5U && packet[0] == 0x00 && packet[1] == 0x40 && packet[2] == 0x04 && packet[3] == 0x4C &&
      packet[4] == 0x70) {
    info.kind = HarmonyPacketKind::PING;
    return info;
  }
  if (*command_id == HARMONY_SLEEP) {
    info.kind = HarmonyPacketKind::SLEEP;
    return info;
  }
  if (*command_id == 0x000000C1U || *command_id == 0x000000C3U) {
    info.kind = HarmonyPacketKind::RELEASE;
    return info;
  }
  if (((*command_id & 0xFF000000U) != 0U) && resolver != nullptr) {
    const auto related = resolver->resolve(*command_id & 0x00FFFFFFU);
    if (related.known) {
      info.kind = HarmonyPacketKind::STATE;
      info.related_command_id = related.id;
      return info;
    }
  }
  if (((*command_id & 0x000000F0U) == 0x000000C0U)) {
    info.kind = HarmonyPacketKind::PRESS;
  }
  return info;
}

std::string describe_harmony_packet(const std::vector<uint8_t> &packet, const HarmonyCommandResolver *resolver) {
  const auto info = classify_harmony_packet(packet, resolver);
  switch (info.kind) {
    case HarmonyPacketKind::HOLD:
      return "hold";
    case HarmonyPacketKind::PING:
      return "ping";
    case HarmonyPacketKind::SLEEP:
      return "sleep";
    case HarmonyPacketKind::RELEASE:
      return "release";
    case HarmonyPacketKind::STATE:
      if (resolver != nullptr) {
        const auto resolved = resolver->resolve(info.related_command_id);
        return "state:" + resolved.name;
      }
      return "state";
    case HarmonyPacketKind::PRESS:
      if (resolver != nullptr) {
        const auto resolved = resolver->resolve(info.command_id);
        return "press:" + resolved.name;
      }
      return "press";
    case HarmonyPacketKind::UNKNOWN:
    default:
      return {};
  }
}

}  // namespace esphome::harmoino
