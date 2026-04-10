#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::harmoino {

inline constexpr uint8_t NRF24_MAX_PAYLOAD_SIZE = 32;
inline constexpr uint8_t NRF24_ADDRESS_WIDTH = 5;

struct Nrf24DebugSnapshot {
  uint8_t status{0};
  uint8_t config{0};
  uint8_t en_aa{0};
  uint8_t en_rxaddr{0};
  uint8_t setup_aw{0};
  uint8_t setup_retr{0};
  uint8_t observe_tx{0};
  uint8_t rf_ch{0};
  uint8_t rf_setup{0};
  uint8_t feature{0};
  uint8_t dynpd{0};
  uint8_t fifo_status{0};
};

enum class Nrf24DebugProfile : uint8_t {
  POST_BEGIN = 0,
  RECEIVER,
  PROBE,
  IDLE,
};

struct Nrf24RegisterCheck {
  const char *name{nullptr};
  bool has_expected{false};
  uint8_t expected{0};
  uint8_t actual{0};
  const char *verdict{nullptr};
};

struct Nrf24DebugReport {
  bool healthy{false};
  std::string summary;
  std::vector<Nrf24RegisterCheck> checks;
  std::vector<std::string> lines;
};

class Nrf24BusInterface {
 public:
  virtual ~Nrf24BusInterface() = default;

  virtual void begin_transaction() = 0;
  virtual void end_transaction() = 0;
  virtual uint8_t transfer_byte(uint8_t data) = 0;
  virtual void write_bytes(const uint8_t *data, size_t length) = 0;
  virtual void read_bytes(uint8_t *data, size_t length) = 0;
  virtual void set_ce(bool level) = 0;
  virtual void delay_microseconds(uint32_t duration) = 0;
  virtual uint32_t millis() = 0;
};

class Nrf24Radio {
 public:
  explicit Nrf24Radio(Nrf24BusInterface *bus) : bus_(bus) {}

  bool begin();
  bool enable_dynamic_payloads();
  bool enable_ack_payload();
  bool set_data_rate_2mbps();
  void set_channel(uint8_t channel);
  void set_crc_16();
  void set_auto_retry(uint8_t delay_steps, uint8_t count);
  void open_reading_pipe(uint8_t pipe, uint64_t address);
  void open_writing_pipe(uint64_t address);
  void start_listening();
  void stop_listening();
  bool available(uint8_t *pipe_num = nullptr);
  bool is_ack_payload_available();
  uint8_t get_dynamic_payload_size();
  void read_payload(uint8_t *buffer, size_t length);
  bool write_payload(const uint8_t *buffer, size_t length, uint32_t timeout_ms = 100);
  void flush_rx();
  void flush_tx();
  void clear_status_flags();
  Nrf24DebugSnapshot read_debug_snapshot();
  Nrf24DebugReport build_debug_report(const char *context, Nrf24DebugProfile profile,
                                      uint8_t expected_channel = 0);

 protected:
  uint8_t get_status_();
  uint8_t read_register_(uint8_t reg);
  void write_register_(uint8_t reg, uint8_t value);
  void write_register_(uint8_t reg, const uint8_t *buffer, size_t length);
  void toggle_features_();
  void write_command_(uint8_t command);
  void address_to_bytes_(uint64_t address, uint8_t *out) const;

  Nrf24BusInterface *bus_;
};

}  // namespace esphome::harmoino
