#pragma once

#include <optional>
#include <string>
#include <vector>

#include "esphome/components/button/button.h"
#include "esphome/components/event/event.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "harmony_protocol.h"
#include "nrf24_radio.h"

namespace esphome::harmoino {

class Harmoino;

class RawPacketTrigger : public Trigger<std::vector<uint8_t>, uint8_t> {
 public:
  explicit RawPacketTrigger(Harmoino *parent);
};
class HarmoinoPressTrigger : public Trigger<std::string> {
 public:
  explicit HarmoinoPressTrigger(Harmoino *parent);
};
class HarmoinoReleaseTrigger : public Trigger<std::string> {
 public:
  explicit HarmoinoReleaseTrigger(Harmoino *parent);
};
class HarmoinoEventTrigger : public Trigger<std::string> {
 public:
  explicit HarmoinoEventTrigger(Harmoino *parent);
};
class AddressTrigger : public Trigger<std::string> {
 public:
  explicit AddressTrigger(Harmoino *parent);
};

class HarmoinoEventEntity : public event::Event {};
class HarmoinoAddressTextSensor : public text_sensor::TextSensor {};

class HarmoinoProbeButton : public button::Button {
 public:
  explicit HarmoinoProbeButton(Harmoino *parent) : parent_(parent) {}

 protected:
  void press_action() override;
  Harmoino *parent_;
};

class HarmoinoProbeSwitch : public switch_::Switch {
 public:
  explicit HarmoinoProbeSwitch(Harmoino *parent) : parent_(parent) {}

 protected:
  void write_state(bool state) override;
  Harmoino *parent_;
};

class HarmoinoSaveButton : public button::Button {
 public:
  explicit HarmoinoSaveButton(Harmoino *parent) : parent_(parent) {}

 protected:
  void press_action() override;
  Harmoino *parent_;
};

using HarmoinoSPIDevice =
    spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_8MHZ>;

class Harmoino : public Component, public HarmoinoSPIDevice, public Nrf24BusInterface {
 public:
  Harmoino() : radio_(this) {}

  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_component_key(const std::string &component_key) { this->component_key_ = component_key; }
  void set_yaml_address(uint64_t address) { this->yaml_address_ = address; }
  void set_yaml_channel(uint8_t channel) { this->yaml_channel_ = channel; }
  void set_ce_pin(InternalGPIOPin *ce_pin) { this->ce_pin_ = ce_pin; }
  void set_click_duration_ms(uint32_t duration) { this->click_duration_ms_ = duration; }
  void set_wait_duration_ms(uint32_t duration) { this->wait_duration_ms_ = duration; }
  void set_second_repeat_duration_ms(uint32_t duration) { this->second_repeat_duration_ms_ = duration; }
  void set_further_repeat_duration_ms(uint32_t duration) { this->further_repeat_duration_ms_ = duration; }
  void set_probe_timeout_ms(uint32_t duration) { this->probe_timeout_ms_ = duration; }
  void add_command_override(uint32_t id, const std::string &name, uint8_t type);
  void set_event_entity(HarmoinoEventEntity *event_entity) { this->event_entity_ = event_entity; }
  void set_discovered_address_sensor(HarmoinoAddressTextSensor *sensor) { this->discovered_address_sensor_ = sensor; }
  void set_saved_address_sensor(HarmoinoAddressTextSensor *sensor) { this->saved_address_sensor_ = sensor; }
  void set_effective_address_sensor(HarmoinoAddressTextSensor *sensor) { this->effective_address_sensor_ = sensor; }
  void set_probe_switch(HarmoinoProbeSwitch *probe_switch) { this->probe_switch_ = probe_switch; }
  void set_probe_on_startup(bool probe_on_startup) { this->probe_on_startup_ = probe_on_startup; }

  void start_probe();
  void stop_probe();
  void save_discovered_address();
  void add_on_raw_packet_callback(std::function<void(const std::vector<uint8_t> &, uint8_t)> &&callback) {
    this->raw_packet_callback_.add(std::move(callback));
  }
  void add_on_press_callback(std::function<void(const std::string &)> &&callback) {
    this->press_callback_.add(std::move(callback));
  }
  void add_on_release_callback(std::function<void(const std::string &)> &&callback) {
    this->release_callback_.add(std::move(callback));
  }
  void add_on_event_callback(std::function<void(const std::string &)> &&callback) {
    this->event_callback_.add(std::move(callback));
  }
  void add_on_address_discovered_callback(std::function<void(const std::string &)> &&callback) {
    this->address_callback_.add(std::move(callback));
  }

  void begin_transaction() override { this->enable(); }
  void end_transaction() override { this->disable(); }
  uint8_t transfer_byte(uint8_t data) override { return this->HarmoinoSPIDevice::transfer_byte(data); }
  void write_bytes(const uint8_t *data, size_t length) override { this->write_array(data, length); }
  void read_bytes(uint8_t *data, size_t length) override { this->read_array(data, length); }
  void set_ce(bool level) override;
  void delay_microseconds(uint32_t duration) override;
  uint32_t millis() override;

 protected:
  struct SavedAddressState {
    uint32_t magic;
    uint64_t address;
  };
  struct SavedChannelState {
    uint32_t magic;
    uint8_t channel;
    uint8_t reserved[3];
  };

  enum class RadioMode : uint8_t {
    IDLE = 0,
    RECEIVER,
    PROBE,
  };

  bool has_event_type_(const std::string &payload) const;
  void emit_output_(const HarmonyOutput &output);
  void process_outputs_(const std::vector<HarmonyOutput> &outputs);
  void process_receiver_packets_(uint32_t now_ms);
  void process_probe_(uint32_t now_ms);
  bool initialize_receiver_mode_();
  bool initialize_probe_mode_();
  bool initialize_idle_mode_();
  void restore_operating_mode_(bool start_probe_if_unconfigured = false);
  void refresh_address_sensors_();
  void publish_address_state_(HarmoinoAddressTextSensor *sensor, std::optional<uint64_t> address);
  void publish_probe_state_();
  void set_probing_(bool probing);
  bool should_auto_save_discovery_(uint64_t address) const;
  bool is_valid_channel_(uint8_t channel) const;
  void on_address_discovered_(uint64_t address, uint8_t channel);
  bool load_saved_address_();
  bool persist_saved_address_();
  bool load_saved_channel_();
  bool persist_saved_channel_();
  void log_radio_snapshot_(const char *context);

  static constexpr uint8_t CHANNELS[12] = {5, 8, 14, 17, 32, 35, 41, 44, 62, 65, 71, 74};
  static constexpr uint64_t PAIRING_ADDRESS = 0xBB0ADCA575ULL;
  static constexpr uint8_t PAIR_MESSAGE[22] = {242, 95, 1, 225, 154, 157, 218, 83, 40, 64, 30,
                                               4,   2,  7, 12, 0,   0,   0,  0,  0,  102, 100};
  static constexpr uint8_t PING_MESSAGE[5] = {242, 64, 1, 225, 236};
  static constexpr uint32_t SAVED_ADDRESS_MAGIC = 0x484D4E31UL;
  static constexpr uint32_t SAVED_CHANNEL_MAGIC = 0x484D4348UL;
  static constexpr uint32_t SCAN_LOG_INTERVAL_MS = 5000;
  static constexpr uint32_t DEFAULT_CHANNEL = 5;
  static constexpr uint32_t DEFAULT_PROBE_TIMEOUT_MS = 60000;

  InternalGPIOPin *ce_pin_{nullptr};
  std::string component_key_;
  std::optional<uint64_t> yaml_address_;
  std::optional<uint8_t> yaml_channel_;
  std::optional<uint64_t> saved_address_;
  std::optional<uint8_t> saved_channel_;
  std::optional<uint64_t> discovered_address_;
  std::optional<uint8_t> discovered_channel_;
  std::optional<uint64_t> effective_address_;
  uint8_t effective_channel_{DEFAULT_CHANNEL};
  uint32_t click_duration_ms_{HarmonyDecoder::DEFAULT_CLICK_DURATION_MS};
  uint32_t wait_duration_ms_{HarmonyDecoder::DEFAULT_WAIT_DURATION_MS};
  uint32_t second_repeat_duration_ms_{HarmonyDecoder::DEFAULT_SECOND_REPEAT_DURATION_MS};
  uint32_t further_repeat_duration_ms_{HarmonyDecoder::DEFAULT_FURTHER_REPEAT_DURATION_MS};
  uint32_t probe_timeout_ms_{DEFAULT_PROBE_TIMEOUT_MS};
  bool probe_on_startup_{true};
  bool probing_{false};
  uint8_t channel_index_{0};
  uint8_t ping_retries_{0};
  uint32_t probe_started_ms_{0};
  uint32_t next_action_ms_{0};
  uint32_t last_scan_log_ms_{0};
  RadioMode radio_mode_{RadioMode::IDLE};
  ESPPreferenceObject saved_address_pref_;
  ESPPreferenceObject saved_channel_pref_;

  HarmonyCommandResolver resolver_;
  HarmonyDecoder decoder_;
  Nrf24Radio radio_;

  HarmoinoEventEntity *event_entity_{nullptr};
  HarmoinoAddressTextSensor *discovered_address_sensor_{nullptr};
  HarmoinoAddressTextSensor *saved_address_sensor_{nullptr};
  HarmoinoAddressTextSensor *effective_address_sensor_{nullptr};
  HarmoinoProbeSwitch *probe_switch_{nullptr};

  CallbackManager<void(const std::vector<uint8_t> &, uint8_t)> raw_packet_callback_;
  CallbackManager<void(const std::string &)> press_callback_;
  CallbackManager<void(const std::string &)> release_callback_;
  CallbackManager<void(const std::string &)> event_callback_;
  CallbackManager<void(const std::string &)> address_callback_;
};

}  // namespace esphome::harmoino
