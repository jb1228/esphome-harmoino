#include "harmoino.h"

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::harmoino {

static const char *const TAG = "harmoino";

constexpr uint8_t Harmoino::CHANNELS[12];
constexpr uint8_t Harmoino::PAIR_MESSAGE[22];
constexpr uint8_t Harmoino::PING_MESSAGE[5];

RawPacketTrigger::RawPacketTrigger(Harmoino *parent) {
  parent->add_on_raw_packet_callback(
      [this](const std::vector<uint8_t> &packet, uint8_t pipe_num) { this->trigger(packet, pipe_num); });
}

HarmoinoRawEventTrigger::HarmoinoRawEventTrigger(Harmoino *parent) {
  parent->add_on_raw_event_callback([this](const std::string &code, const std::string &name, int32_t value) {
    this->trigger(code, name, value);
  });
}

HarmoinoEventTrigger::HarmoinoEventTrigger(Harmoino *parent) {
  parent->add_on_event_callback([this](const std::string &payload) { this->trigger(payload); });
}

AddressTrigger::AddressTrigger(Harmoino *parent) {
  parent->add_on_address_discovered_callback([this](const std::string &address) { this->trigger(address); });
}

void HarmoinoProbeButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->start_probe();
  }
}

void HarmoinoProbeSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    this->publish_state(false);
    return;
  }

  if (state) {
    this->parent_->start_probe();
  } else {
    this->parent_->stop_probe();
  }
}

void HarmoinoSaveButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->save_discovered_address();
  }
}

void Harmoino::setup() {
  if (this->ce_pin_ == nullptr) {
    ESP_LOGE(TAG, "Missing CE pin configuration");
    this->mark_failed();
    return;
  }

  this->ce_pin_->setup();
  this->ce_pin_->digital_write(false);
  this->spi_setup();

  this->decoder_.set_command_resolver(&this->resolver_);
  this->decoder_.set_timings(this->click_duration_ms_, this->wait_duration_ms_, this->second_repeat_duration_ms_,
                             this->further_repeat_duration_ms_);

  if (global_preferences != nullptr && !this->component_key_.empty()) {
    const uint32_t address_hash = fnv1_hash(std::string("harmoino:address:") + this->component_key_);
    const uint32_t channel_hash = fnv1_hash(std::string("harmoino:channel:") + this->component_key_);
    this->saved_address_pref_ = global_preferences->make_preference<SavedAddressState>(address_hash, true);
    this->saved_channel_pref_ = global_preferences->make_preference<SavedChannelState>(channel_hash, true);
    this->load_saved_address_();
    this->load_saved_channel_();
  }

  this->publish_probe_state_();
  this->restore_operating_mode_(true);
}

void Harmoino::loop() {
  if (this->is_failed()) {
    return;
  }

  const uint32_t now_ms = esphome::millis();
  if (this->probing_) {
    this->process_probe_(now_ms);
    return;
  }

  if (this->radio_mode_ == RadioMode::RECEIVER) {
    this->process_receiver_packets_(now_ms);
  } else {
    this->process_outputs_(this->decoder_.poll(now_ms));
  }
}

void Harmoino::dump_config() {
  ESP_LOGCONFIG(TAG, "Harmoino");
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Initialization failed");
  }
  LOG_PIN("  CE Pin: ", this->ce_pin_);
  LOG_PIN("  CS Pin: ", this->cs_);
  if (this->yaml_address_.has_value()) {
    ESP_LOGCONFIG(TAG, "  YAML Address: %s", format_address_text(this->yaml_address_).c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  YAML Address: none");
  }
  if (this->saved_address_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Saved Address: %s", format_address_text(this->saved_address_).c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  Saved Address: none");
  }
  if (this->effective_address_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Effective Address: %s", format_address_text(this->effective_address_).c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  Effective Address: none");
  }
  if (this->yaml_channel_.has_value()) {
    ESP_LOGCONFIG(TAG, "  YAML Channel: %u", *this->yaml_channel_);
  } else {
    ESP_LOGCONFIG(TAG, "  YAML Channel: none");
  }
  if (this->saved_channel_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Saved Channel: %u", *this->saved_channel_);
  } else {
    ESP_LOGCONFIG(TAG, "  Saved Channel: none");
  }
  if (this->discovered_channel_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Discovered Channel: %u", *this->discovered_channel_);
  } else {
    ESP_LOGCONFIG(TAG, "  Discovered Channel: none");
  }
  ESP_LOGCONFIG(TAG, "  Effective Channel: %u", this->effective_channel_);
  ESP_LOGCONFIG(TAG, "  Long Press Threshold (click_duration): %lu ms", this->click_duration_ms_);
  ESP_LOGCONFIG(TAG, "  Gesture Gap Timeout (wait_duration): %lu ms", this->wait_duration_ms_);
  ESP_LOGCONFIG(TAG, "  First Repeat Delay (second_repeat_duration): %lu ms", this->second_repeat_duration_ms_);
  ESP_LOGCONFIG(TAG, "  Repeat Interval (further_repeat_duration): %lu ms", this->further_repeat_duration_ms_);
  ESP_LOGCONFIG(TAG, "  Probe Timeout: %lu ms", this->probe_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Probe On Startup: %s", YESNO(this->probe_on_startup_));
  ESP_LOGCONFIG(TAG, "  Probe Active: %s", YESNO(this->probing_));
  this->log_radio_snapshot_("dump");
}

void Harmoino::add_command_override(uint32_t id, const std::string &name, uint8_t type) {
  this->resolver_.add_override(id, name, type);
}

void Harmoino::set_ce(bool level) {
  if (this->ce_pin_ != nullptr) {
    this->ce_pin_->digital_write(level);
  }
}

void Harmoino::delay_microseconds(uint32_t duration) { esphome::delayMicroseconds(duration); }

uint32_t Harmoino::millis() { return esphome::millis(); }

bool Harmoino::has_event_type_(const std::string &payload) const {
  if (this->event_entity_ == nullptr) {
    return false;
  }
  for (const char *event_type : this->event_entity_->get_event_types()) {
    if (payload == event_type) {
      return true;
    }
  }
  return false;
}

void Harmoino::emit_output_(const HarmonyOutput &output) {
  if (output.kind == HarmonyOutputKind::PRESS || output.kind == HarmonyOutputKind::RELEASE ||
      output.kind == HarmonyOutputKind::RAW_EVENT) {
    const std::string code = "0x" + format_hex_value(output.command_id, 8);
    const int32_t value = output.kind == HarmonyOutputKind::RELEASE ? 0 : 1;
    ESP_LOGD(TAG, "Publishing Harmony raw event: code=%s name=%s value=%ld", code.c_str(), output.payload.c_str(),
             value);
    this->raw_event_callback_.call(code, output.payload, value);
    return;
  }

  ESP_LOGD(TAG, "Publishing Harmony event: %s", output.payload.c_str());
  this->event_callback_.call(output.payload);

  if (this->event_entity_ == nullptr) {
    return;
  }
  if (!this->has_event_type_(output.payload)) {
    ESP_LOGW(TAG, "Skipping unmapped event entity payload '%s' for command 0x%08lX", output.payload.c_str(),
             output.command_id);
    return;
  }
  this->event_entity_->trigger(output.payload);
}

void Harmoino::process_outputs_(const std::vector<HarmonyOutput> &outputs) {
  for (const auto &output : outputs) {
    this->emit_output_(output);
  }
}

void Harmoino::process_receiver_packets_(uint32_t now_ms) {
  uint8_t pipe_num = 0;
  while (this->radio_.available(&pipe_num)) {
    const uint8_t payload_size = this->radio_.get_dynamic_payload_size();
    if (payload_size == 0 || payload_size > NRF24_MAX_PAYLOAD_SIZE) {
      continue;
    }

    std::vector<uint8_t> packet(payload_size);
    this->radio_.read_payload(packet.data(), packet.size());
    const auto description = describe_harmony_packet(packet, &this->resolver_);
    if (!description.empty()) {
      ESP_LOGV(TAG, "Raw packet on pipe %u with %u bytes: %s [%s]", pipe_num, payload_size,
               format_hex_bytes(packet.data(), packet.size()).c_str(), description.c_str());
    } else {
      ESP_LOGI(TAG, "Unknown raw packet on pipe %u with %u bytes: %s", pipe_num, payload_size,
               format_hex_bytes(packet.data(), packet.size()).c_str());
    }
    if (const auto command_id = extract_harmony_command_id(packet); command_id.has_value()) {
      ESP_LOGVV(TAG, "Decoded Harmony command 0x%08X from pipe %u", *command_id, pipe_num);
    }

    this->raw_packet_callback_.call(packet, pipe_num);
    this->process_outputs_(this->decoder_.process_packet(packet, now_ms));
  }

  this->process_outputs_(this->decoder_.poll(now_ms));
}

bool Harmoino::initialize_receiver_mode_() {
  if (!this->effective_address_.has_value()) {
    return this->initialize_idle_mode_();
  }

  this->decoder_.reset();
  this->decoder_.set_command_resolver(&this->resolver_);
  this->decoder_.set_timings(this->click_duration_ms_, this->wait_duration_ms_, this->second_repeat_duration_ms_,
                             this->further_repeat_duration_ms_);

  if (!this->radio_.begin()) {
    ESP_LOGE(TAG, "nRF24L01+ radio hardware not responding");
    this->mark_failed();
    return false;
  }
  if (!this->radio_.set_data_rate_2mbps()) {
    ESP_LOGE(TAG, "Unable to set nRF24L01+ data rate to 2Mbps");
    this->mark_failed();
    return false;
  }
  if (!this->radio_.enable_dynamic_payloads()) {
    ESP_LOGE(TAG, "Unable to enable nRF24L01+ dynamic payloads");
    this->mark_failed();
    return false;
  }

  this->radio_.set_crc_16();
  this->radio_.open_reading_pipe(1, *this->effective_address_ & 0xFFFFFFFF00ULL);
  this->radio_.open_reading_pipe(2, *this->effective_address_ & 0xFFFFFFFFFFULL);
  this->radio_.set_channel(this->effective_channel_);
  this->radio_.start_listening();
  this->radio_mode_ = RadioMode::RECEIVER;
  ESP_LOGI(TAG, "Listening for Harmony packets on %s channel %u", format_address_text(this->effective_address_).c_str(),
           this->effective_channel_);
  return true;
}

bool Harmoino::initialize_probe_mode_() {
  this->decoder_.reset();

  if (!this->radio_.begin()) {
    ESP_LOGE(TAG, "nRF24L01+ radio hardware not responding");
    this->mark_failed();
    return false;
  }
  if (!this->radio_.set_data_rate_2mbps()) {
    ESP_LOGE(TAG, "Unable to set nRF24L01+ data rate to 2Mbps");
    this->mark_failed();
    return false;
  }
  if (!this->radio_.enable_ack_payload()) {
    ESP_LOGE(TAG, "Unable to enable nRF24L01+ ACK payload support");
    this->mark_failed();
    return false;
  }

  this->radio_.set_crc_16();
  this->radio_.open_writing_pipe(PAIRING_ADDRESS);
  this->radio_.stop_listening();
  this->radio_mode_ = RadioMode::PROBE;
  ESP_LOGI(TAG, "Scanning Harmony pairing channels; press the pair/reset button on the hub now");
  return true;
}

bool Harmoino::initialize_idle_mode_() {
  this->decoder_.reset();

  if (!this->radio_.begin()) {
    ESP_LOGE(TAG, "nRF24L01+ radio hardware not responding");
    this->mark_failed();
    return false;
  }
  if (!this->radio_.set_data_rate_2mbps()) {
    ESP_LOGE(TAG, "Unable to set nRF24L01+ data rate to 2Mbps");
    this->mark_failed();
    return false;
  }
  if (!this->radio_.enable_dynamic_payloads()) {
    ESP_LOGE(TAG, "Unable to enable nRF24L01+ dynamic payloads");
    this->mark_failed();
    return false;
  }

  this->radio_.set_crc_16();
  this->radio_.stop_listening();
  this->radio_mode_ = RadioMode::IDLE;
  ESP_LOGI(TAG, "No active Harmony address configured; receiver idle until probe/save or YAML address is provided");
  return true;
}

void Harmoino::restore_operating_mode_(bool start_probe_if_unconfigured) {
  if (this->probing_) {
    return;
  }

  this->effective_address_ = select_effective_address(this->yaml_address_, this->saved_address_);
  if (this->yaml_channel_.has_value()) {
    this->effective_channel_ = *this->yaml_channel_;
  } else if (this->saved_channel_.has_value()) {
    this->effective_channel_ = *this->saved_channel_;
  } else {
    this->effective_channel_ = DEFAULT_CHANNEL;
  }
  this->refresh_address_sensors_();
  if (this->effective_address_.has_value()) {
    this->initialize_receiver_mode_();
  } else if (start_probe_if_unconfigured && this->probe_on_startup_) {
    ESP_LOGI(TAG, "No YAML or saved Harmony address available at startup; starting automatic probe");
    this->start_probe();
  } else {
    this->initialize_idle_mode_();
  }
}

void Harmoino::publish_address_state_(HarmoinoAddressTextSensor *sensor, std::optional<uint64_t> address) {
  if (sensor == nullptr) {
    return;
  }
  sensor->publish_state(format_address_text(address));
}

void Harmoino::refresh_address_sensors_() {
  this->publish_address_state_(this->discovered_address_sensor_, this->discovered_address_);
  this->publish_address_state_(this->saved_address_sensor_, this->saved_address_);
  this->publish_address_state_(this->effective_address_sensor_, this->effective_address_);
}

void Harmoino::publish_probe_state_() {
  if (this->probe_switch_ != nullptr) {
    this->probe_switch_->publish_state(this->probing_);
  }
}

void Harmoino::set_probing_(bool probing) {
  if (this->probing_ == probing) {
    return;
  }
  this->probing_ = probing;
  this->publish_probe_state_();
}

bool Harmoino::should_auto_save_discovery_(uint64_t address) const {
  if (this->yaml_address_.has_value()) {
    return false;
  }
  if (!this->saved_address_.has_value()) {
    return true;
  }
  return !this->saved_channel_.has_value() && *this->saved_address_ == address;
}

bool Harmoino::is_valid_channel_(uint8_t channel) const {
  return std::find(std::begin(CHANNELS), std::end(CHANNELS), channel) != std::end(CHANNELS);
}

void Harmoino::on_address_discovered_(uint64_t address, uint8_t channel) {
  this->discovered_address_ = address;
  this->discovered_channel_ = channel;
  this->publish_address_state_(this->discovered_address_sensor_, this->discovered_address_);
  const std::string address_text = format_address_text(this->discovered_address_);
  ESP_LOGI(TAG, "Discovered Harmony RF address %s on channel %u", address_text.c_str(), channel);
  this->address_callback_.call(address_text);
}

bool Harmoino::load_saved_address_() {
  SavedAddressState state{};
  if (!this->saved_address_pref_.load(&state)) {
    return false;
  }
  if (state.magic != SAVED_ADDRESS_MAGIC || state.address == 0 || state.address > 0xFFFFFFFFFFULL) {
    return false;
  }
  this->saved_address_ = state.address;
  return true;
}

bool Harmoino::persist_saved_address_() {
  if (!this->saved_address_.has_value()) {
    return false;
  }

  SavedAddressState state{SAVED_ADDRESS_MAGIC, *this->saved_address_};
  if (!this->saved_address_pref_.save(&state)) {
    return false;
  }
  if (global_preferences != nullptr) {
    global_preferences->sync();
  }
  return true;
}

bool Harmoino::load_saved_channel_() {
  SavedChannelState state{};
  if (!this->saved_channel_pref_.load(&state)) {
    return false;
  }
  if (state.magic != SAVED_CHANNEL_MAGIC || !this->is_valid_channel_(state.channel)) {
    return false;
  }
  this->saved_channel_ = state.channel;
  return true;
}

bool Harmoino::persist_saved_channel_() {
  if (!this->saved_channel_.has_value()) {
    return false;
  }

  SavedChannelState state{};
  state.magic = SAVED_CHANNEL_MAGIC;
  state.channel = *this->saved_channel_;
  if (!this->saved_channel_pref_.save(&state)) {
    return false;
  }
  if (global_preferences != nullptr) {
    global_preferences->sync();
  }
  return true;
}

void Harmoino::start_probe() {
  if (this->is_failed()) {
    this->publish_probe_state_();
    return;
  }
  if (this->probing_) {
    ESP_LOGD(TAG, "Harmony address probe already running");
    this->publish_probe_state_();
    return;
  }

  this->set_probing_(true);
  this->channel_index_ = 0;
  this->ping_retries_ = 0;
  this->probe_started_ms_ = esphome::millis();
  this->next_action_ms_ = 0;
  this->last_scan_log_ms_ = 0;
  if (!this->initialize_probe_mode_()) {
    this->set_probing_(false);
    return;
  }
}

void Harmoino::stop_probe() {
  if (!this->probing_) {
    this->publish_probe_state_();
    return;
  }

  this->set_probing_(false);
  ESP_LOGI(TAG, "Stopped Harmony address probe");
  this->restore_operating_mode_();
}

void Harmoino::save_discovered_address() {
  if (!this->discovered_address_.has_value() || !this->discovered_channel_.has_value()) {
    ESP_LOGW(TAG, "No discovered Harmony address/channel available to save");
    return;
  }

  this->saved_address_ = this->discovered_address_;
  this->saved_channel_ = this->discovered_channel_;
  if (!this->persist_saved_address_() || !this->persist_saved_channel_()) {
    ESP_LOGW(TAG, "Failed to persist Harmony address %s", format_address_text(this->saved_address_).c_str());
    return;
  }

  ESP_LOGI(TAG, "Saved Harmony RF address %s on channel %u", format_address_text(this->saved_address_).c_str(),
           *this->saved_channel_);
  if (!this->yaml_address_.has_value() && !this->probing_) {
    this->restore_operating_mode_();
  } else {
    this->refresh_address_sensors_();
  }
}

void Harmoino::process_probe_(uint32_t now_ms) {
  if (this->probe_timeout_ms_ > 0 && (now_ms - this->probe_started_ms_) >= this->probe_timeout_ms_) {
    ESP_LOGW(TAG, "Harmony address probe timed out after %lu ms", this->probe_timeout_ms_);
    this->stop_probe();
    return;
  }

  if (now_ms >= this->next_action_ms_) {
    const uint8_t channel = CHANNELS[this->channel_index_];
    if (this->ping_retries_ == 0) {
      this->radio_.set_channel(channel);
      ESP_LOGD(TAG, "Sending Harmony pair request on RF channel %u", channel);
      if (this->radio_.write_payload(PAIR_MESSAGE, sizeof(PAIR_MESSAGE))) {
        this->ping_retries_ = 10;
        ESP_LOGD(TAG, "Pair request transmitted on channel %u; sending follow-up probe pings", channel);
      } else {
        ESP_LOGW(TAG, "Pair request TX failed on channel %u; advancing to the next scan channel", channel);
        this->channel_index_ = static_cast<uint8_t>((this->channel_index_ + 1) % 12);
      }
    } else {
      const bool ping_sent = this->radio_.write_payload(PING_MESSAGE, sizeof(PING_MESSAGE));
      ESP_LOGV(TAG, "Probe ping on RF channel %u %s; %u retries remain", channel, ping_sent ? "sent" : "failed",
               this->ping_retries_ - 1);
      this->ping_retries_--;
    }
    this->next_action_ms_ = now_ms + 100;
  }

  if ((now_ms - this->last_scan_log_ms_) >= SCAN_LOG_INTERVAL_MS) {
    this->last_scan_log_ms_ = now_ms;
    ESP_LOGD(TAG, "Still scanning for Harmony pair response; current RF channel %u, phase=%s, pending_pings=%u",
             CHANNELS[this->channel_index_], this->ping_retries_ == 0 ? "pair_request" : "probe_ping",
             this->ping_retries_);
  }

  if (!this->radio_.is_ack_payload_available()) {
    return;
  }

  const uint8_t payload_size = this->radio_.get_dynamic_payload_size();
  if (payload_size == 0 || payload_size > NRF24_MAX_PAYLOAD_SIZE) {
    ESP_LOGW(TAG, "Ignoring invalid ACK payload size %u", payload_size);
    return;
  }

  std::vector<uint8_t> payload(payload_size);
  this->radio_.read_payload(payload.data(), payload.size());
  ESP_LOGD(TAG, "ACK payload (%u bytes): %s", payload_size, format_hex_bytes(payload.data(), payload.size()).c_str());
  const auto address = extract_remote_address(payload);
  if (!address.has_value()) {
    ESP_LOGD(TAG, "ACK payload does not match the expected Harmony address response");
    return;
  }

  const uint8_t discovered_channel = CHANNELS[this->channel_index_];
  this->set_probing_(false);
  this->on_address_discovered_(*address, discovered_channel);
  if (this->should_auto_save_discovery_(*address)) {
    this->saved_address_ = *address;
    this->saved_channel_ = discovered_channel;
    if (!this->persist_saved_address_() || !this->persist_saved_channel_()) {
      ESP_LOGW(TAG, "Failed to persist discovered Harmony address %s on channel %u",
               format_address_text(this->saved_address_).c_str(), discovered_channel);
    } else {
      ESP_LOGI(TAG, "Automatically saved Harmony RF address %s on channel %u",
               format_address_text(this->saved_address_).c_str(), discovered_channel);
    }
  }
  this->restore_operating_mode_();
}

void Harmoino::log_radio_snapshot_(const char *context) {
  Nrf24DebugProfile profile = Nrf24DebugProfile::POST_BEGIN;
  uint8_t expected_channel = 0;

  switch (this->radio_mode_) {
    case RadioMode::RECEIVER:
      profile = Nrf24DebugProfile::RECEIVER;
      expected_channel = this->effective_channel_;
      break;
    case RadioMode::PROBE:
      profile = Nrf24DebugProfile::PROBE;
      break;
    case RadioMode::IDLE:
      profile = Nrf24DebugProfile::IDLE;
      break;
  }

  const auto report = this->radio_.build_debug_report(context, profile, expected_channel);
  for (const auto &line : report.lines) {
    ESP_LOGD(TAG, "%s", line.c_str());
  }
}

}  // namespace esphome::harmoino
