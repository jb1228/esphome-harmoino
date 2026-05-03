#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace esphome::harmoino {

struct HarmonyCommandDefinition {
  uint32_t id;
  uint8_t type;
  const char *name;
};

struct HarmonyCommandOverride {
  uint32_t id;
  uint8_t type;
  std::string name;
};

struct HarmonyResolvedCommand {
  uint32_t id{0};
  uint8_t type{0};
  std::string name;
  bool known{false};
};

enum class HarmonyOutputKind : uint8_t {
  SEMANTIC = 0,
  PRESS,
  RELEASE,
  RAW_EVENT,
};

struct HarmonyOutput {
  uint32_t command_id{0};
  std::string payload;
  bool known{false};
  HarmonyOutputKind kind{HarmonyOutputKind::SEMANTIC};
};

enum class HarmonyPacketKind : uint8_t {
  UNKNOWN = 0,
  PRESS,
  RELEASE,
  HOLD,
  PING,
  SLEEP,
  STATE,
};

struct HarmonyPacketInfo {
  HarmonyPacketKind kind{HarmonyPacketKind::UNKNOWN};
  uint32_t command_id{0};
  uint32_t related_command_id{0};
};

class HarmonyCommandResolver {
 public:
  void add_override(uint32_t id, const std::string &name, uint8_t type);
  HarmonyResolvedCommand resolve(uint32_t id) const;
  std::vector<std::string> build_event_types() const;

 protected:
  const HarmonyCommandDefinition *find_default_(uint32_t id) const;
  const HarmonyCommandOverride *find_override_(uint32_t id) const;
  static void append_payload_types_(std::vector<std::string> &event_types, const std::string &name, uint8_t type);

  std::vector<HarmonyCommandOverride> overrides_;
};

class HarmonyDecoder {
 public:
  static constexpr uint32_t DEFAULT_CLICK_DURATION_MS = 450;
  static constexpr uint32_t DEFAULT_WAIT_DURATION_MS = 225;
  static constexpr uint32_t DEFAULT_SECOND_REPEAT_DURATION_MS = 600;
  static constexpr uint32_t DEFAULT_FURTHER_REPEAT_DURATION_MS = 150;
  static constexpr uint32_t RELEASE_CHATTER_MAX_GAP_MS = 60;
  static constexpr uint32_t DEFAULT_SLEEP_TIMEOUT_MS = 2000;

  void set_command_resolver(const HarmonyCommandResolver *resolver) { this->resolver_ = resolver; }
  void set_timings(uint32_t click_duration_ms, uint32_t wait_duration_ms, uint32_t second_repeat_duration_ms,
                   uint32_t further_repeat_duration_ms, uint32_t sleep_timeout_ms = DEFAULT_SLEEP_TIMEOUT_MS);

  std::vector<HarmonyOutput> process_packet(const std::vector<uint8_t> &packet, uint32_t now_ms);
  std::vector<HarmonyOutput> process_command_id(uint32_t command_id, uint32_t now_ms);
  std::vector<HarmonyOutput> poll(uint32_t now_ms);
  void reset();

 protected:
  HarmonyResolvedCommand resolve_(uint32_t command_id) const;
  void begin_sequence_(const HarmonyResolvedCommand &command, uint32_t now_ms);
  void clear_sequence_();
  std::vector<HarmonyOutput> finalize_click_sequence_();
  bool should_treat_press_as_hold_continuation_(uint32_t now_ms) const;
  bool current_press_active_(uint32_t now_ms) const;
  std::vector<HarmonyOutput> handle_press_(uint32_t command_id, uint32_t now_ms);
  std::vector<HarmonyOutput> handle_release_(uint32_t command_id, uint32_t now_ms);
  std::vector<HarmonyOutput> handle_hold_(uint32_t now_ms);
  std::vector<HarmonyOutput> handle_ping_(uint32_t now_ms);
  std::vector<HarmonyOutput> handle_sleep_(uint32_t now_ms);

  const HarmonyCommandResolver *resolver_{nullptr};
  uint32_t click_duration_ms_{DEFAULT_CLICK_DURATION_MS};
  uint32_t wait_duration_ms_{DEFAULT_WAIT_DURATION_MS};
  uint32_t second_repeat_duration_ms_{DEFAULT_SECOND_REPEAT_DURATION_MS};
  uint32_t further_repeat_duration_ms_{DEFAULT_FURTHER_REPEAT_DURATION_MS};
  uint32_t sleep_timeout_ms_{DEFAULT_SLEEP_TIMEOUT_MS};

  bool sequence_active_{false};
  bool pending_release_{false};
  bool release_emitted_{false};
  bool current_press_hold_seen_{false};
  bool long_emitted_{false};
  bool initial_emitted_{false};
  bool awake_{false};
  bool ping_observed_{false};
  uint8_t repeat_stage_{0};
  uint32_t click_count_{0};
  uint32_t current_press_start_ms_{0};
  uint32_t last_press_ms_{0};
  uint32_t last_release_ms_{0};
  uint32_t last_hold_ms_{0};
  uint32_t last_related_ms_{0};
  uint32_t last_activity_ms_{0};
  uint32_t next_repeat_ms_{0};
  HarmonyResolvedCommand current_command_{};
};

std::optional<uint32_t> extract_harmony_command_id(const std::vector<uint8_t> &packet);
std::optional<uint64_t> extract_remote_address(const std::vector<uint8_t> &payload);
std::optional<uint64_t> select_effective_address(std::optional<uint64_t> yaml_address,
                                                 std::optional<uint64_t> saved_address);
std::string format_address_text(std::optional<uint64_t> address);
std::string format_hex_value(uint64_t value, uint8_t width);
std::string format_hex_bytes(const uint8_t *data, size_t length);
HarmonyPacketInfo classify_harmony_packet(const std::vector<uint8_t> &packet,
                                         const HarmonyCommandResolver *resolver = nullptr);
std::string describe_harmony_packet(const std::vector<uint8_t> &packet,
                                    const HarmonyCommandResolver *resolver = nullptr);
const std::vector<HarmonyCommandDefinition> &default_harmony_commands();

inline constexpr uint32_t HARMONY_HOLD = 0x98280040;
inline constexpr uint32_t HARMONY_PING = 0x704C0440;
inline constexpr uint32_t HARMONY_SLEEP = 0x0000034F;

}  // namespace esphome::harmoino
