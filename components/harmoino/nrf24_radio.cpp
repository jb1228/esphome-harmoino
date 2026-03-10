#include "nrf24_radio.h"

namespace esphome::harmoino {
namespace {

constexpr uint8_t REGISTER_MASK = 0x1F;

constexpr uint8_t R_REGISTER = 0x00;
constexpr uint8_t W_REGISTER = 0x20;
constexpr uint8_t R_RX_PAYLOAD = 0x61;
constexpr uint8_t W_TX_PAYLOAD = 0xA0;
constexpr uint8_t FLUSH_TX = 0xE1;
constexpr uint8_t FLUSH_RX = 0xE2;
constexpr uint8_t R_RX_PL_WID = 0x60;
constexpr uint8_t ACTIVATE = 0x50;
constexpr uint8_t NOP = 0xFF;

constexpr uint8_t CONFIG = 0x00;
constexpr uint8_t EN_AA = 0x01;
constexpr uint8_t EN_RXADDR = 0x02;
constexpr uint8_t SETUP_AW = 0x03;
constexpr uint8_t SETUP_RETR = 0x04;
constexpr uint8_t RF_CH = 0x05;
constexpr uint8_t RF_SETUP = 0x06;
constexpr uint8_t STATUS = 0x07;
constexpr uint8_t OBSERVE_TX = 0x08;
constexpr uint8_t RX_ADDR_P0 = 0x0A;
constexpr uint8_t RX_ADDR_P1 = 0x0B;
constexpr uint8_t RX_ADDR_P2 = 0x0C;
constexpr uint8_t TX_ADDR = 0x10;
constexpr uint8_t FIFO_STATUS = 0x17;
constexpr uint8_t DYNPD = 0x1C;
constexpr uint8_t FEATURE = 0x1D;

constexpr uint8_t MASK_RX_DR = 0x40;
constexpr uint8_t MASK_TX_DS = 0x20;
constexpr uint8_t MASK_MAX_RT = 0x10;

constexpr uint8_t EN_CRC = 0x08;
constexpr uint8_t CRCO = 0x04;
constexpr uint8_t PWR_UP = 0x02;
constexpr uint8_t PRIM_RX = 0x01;

constexpr uint8_t RF_DR_LOW = 0x20;
constexpr uint8_t RF_DR_HIGH = 0x08;

constexpr uint8_t RX_EMPTY = 0x01;

constexpr uint8_t EN_DPL = 0x04;
constexpr uint8_t EN_ACK_PAY = 0x02;

}  // namespace

bool Nrf24Radio::begin() {
  this->bus_->set_ce(false);
  this->bus_->delay_microseconds(5000);

  this->write_register_(CONFIG, EN_CRC | CRCO);
  this->write_register_(EN_AA, 0x3F);
  this->write_register_(EN_RXADDR, 0x03);
  this->write_register_(SETUP_AW, 0x03);
  this->set_auto_retry(5, 15);
  this->write_register_(RF_CH, 0x02);
  this->write_register_(RF_SETUP, 0x0E);
  this->write_register_(FEATURE, 0x00);
  this->write_register_(DYNPD, 0x00);
  this->clear_status_flags();
  this->flush_rx();
  this->flush_tx();

  return this->read_register_(SETUP_AW) == 0x03;
}

bool Nrf24Radio::enable_dynamic_payloads() {
  uint8_t feature = this->read_register_(FEATURE);
  this->write_register_(FEATURE, feature | EN_DPL);
  if ((this->read_register_(FEATURE) & EN_DPL) == 0) {
    this->toggle_features_();
    feature = this->read_register_(FEATURE);
    this->write_register_(FEATURE, feature | EN_DPL);
  }
  this->write_register_(DYNPD, 0x3F);
  return (this->read_register_(FEATURE) & EN_DPL) != 0;
}

bool Nrf24Radio::enable_ack_payload() {
  if (!this->enable_dynamic_payloads()) {
    return false;
  }

  uint8_t feature = this->read_register_(FEATURE);
  this->write_register_(FEATURE, feature | EN_ACK_PAY | EN_DPL);
  if ((this->read_register_(FEATURE) & EN_ACK_PAY) == 0) {
    this->toggle_features_();
    feature = this->read_register_(FEATURE);
    this->write_register_(FEATURE, feature | EN_ACK_PAY | EN_DPL);
  }
  this->write_register_(DYNPD, 0x3F);
  return (this->read_register_(FEATURE) & EN_ACK_PAY) != 0;
}

bool Nrf24Radio::set_data_rate_2mbps() {
  uint8_t value = this->read_register_(RF_SETUP);
  value &= static_cast<uint8_t>(~(RF_DR_LOW | RF_DR_HIGH));
  value |= RF_DR_HIGH;
  this->write_register_(RF_SETUP, value);
  value = this->read_register_(RF_SETUP);
  return (value & RF_DR_HIGH) != 0 && (value & RF_DR_LOW) == 0;
}

void Nrf24Radio::set_channel(uint8_t channel) { this->write_register_(RF_CH, channel & 0x7F); }

void Nrf24Radio::set_crc_16() {
  uint8_t config = this->read_register_(CONFIG);
  config |= EN_CRC | CRCO;
  this->write_register_(CONFIG, config);
}

void Nrf24Radio::set_auto_retry(uint8_t delay_steps, uint8_t count) {
  this->write_register_(SETUP_RETR, static_cast<uint8_t>(((delay_steps & 0x0F) << 4) | (count & 0x0F)));
}

void Nrf24Radio::open_reading_pipe(uint8_t pipe, uint64_t address) {
  if (pipe > 5) {
    return;
  }

  uint8_t address_bytes[NRF24_ADDRESS_WIDTH];
  this->address_to_bytes_(address, address_bytes);

  if (pipe <= 1) {
    this->write_register_(static_cast<uint8_t>(RX_ADDR_P0 + pipe), address_bytes, NRF24_ADDRESS_WIDTH);
  } else {
    this->write_register_(static_cast<uint8_t>(RX_ADDR_P2 + (pipe - 2)), address_bytes[0]);
  }

  uint8_t enabled = this->read_register_(EN_RXADDR);
  this->write_register_(EN_RXADDR, static_cast<uint8_t>(enabled | (1U << pipe)));
}

void Nrf24Radio::open_writing_pipe(uint64_t address) {
  uint8_t address_bytes[NRF24_ADDRESS_WIDTH];
  this->address_to_bytes_(address, address_bytes);
  this->write_register_(RX_ADDR_P0, address_bytes, NRF24_ADDRESS_WIDTH);
  this->write_register_(TX_ADDR, address_bytes, NRF24_ADDRESS_WIDTH);

  uint8_t enabled = this->read_register_(EN_RXADDR);
  this->write_register_(EN_RXADDR, static_cast<uint8_t>(enabled | 0x01));
}

void Nrf24Radio::start_listening() {
  this->clear_status_flags();
  uint8_t config = this->read_register_(CONFIG);
  config |= PWR_UP | PRIM_RX;
  this->write_register_(CONFIG, config);
  this->bus_->delay_microseconds(150);
  this->bus_->set_ce(true);
}

void Nrf24Radio::stop_listening() {
  this->bus_->set_ce(false);
  this->bus_->delay_microseconds(150);
  uint8_t config = this->read_register_(CONFIG);
  config |= PWR_UP;
  config &= static_cast<uint8_t>(~PRIM_RX);
  this->write_register_(CONFIG, config);
  this->bus_->delay_microseconds(150);
}

bool Nrf24Radio::available(uint8_t *pipe_num) {
  if ((this->read_register_(FIFO_STATUS) & RX_EMPTY) != 0) {
    return false;
  }
  if (pipe_num != nullptr) {
    *pipe_num = static_cast<uint8_t>((this->get_status_() >> 1) & 0x07);
  }
  return true;
}

bool Nrf24Radio::is_ack_payload_available() { return this->available(nullptr); }

uint8_t Nrf24Radio::get_dynamic_payload_size() {
  this->bus_->begin_transaction();
  this->bus_->transfer_byte(R_RX_PL_WID);
  const uint8_t length = this->bus_->transfer_byte(0);
  this->bus_->end_transaction();

  if (length > NRF24_MAX_PAYLOAD_SIZE) {
    this->flush_rx();
    this->write_register_(STATUS, MASK_RX_DR);
    return 0;
  }
  return length;
}

void Nrf24Radio::read_payload(uint8_t *buffer, size_t length) {
  this->bus_->begin_transaction();
  this->bus_->transfer_byte(R_RX_PAYLOAD);
  this->bus_->read_bytes(buffer, length);
  this->bus_->end_transaction();

  if ((this->read_register_(FIFO_STATUS) & RX_EMPTY) != 0) {
    this->write_register_(STATUS, MASK_RX_DR);
  }
}

bool Nrf24Radio::write_payload(const uint8_t *buffer, size_t length, uint32_t timeout_ms) {
  this->stop_listening();
  this->write_register_(STATUS, MASK_TX_DS | MASK_MAX_RT | MASK_RX_DR);

  this->bus_->begin_transaction();
  this->bus_->transfer_byte(W_TX_PAYLOAD);
  this->bus_->write_bytes(buffer, length);
  this->bus_->end_transaction();

  this->bus_->set_ce(true);
  this->bus_->delay_microseconds(20);
  this->bus_->set_ce(false);

  const uint32_t started = this->bus_->millis();
  while (true) {
    const uint8_t status = this->get_status_();
    if ((status & (MASK_TX_DS | MASK_MAX_RT)) != 0) {
      if ((status & MASK_MAX_RT) != 0) {
        this->write_register_(STATUS, MASK_MAX_RT);
        this->flush_tx();
        return false;
      }
      this->write_register_(STATUS, MASK_TX_DS);
      return true;
    }
    if ((this->bus_->millis() - started) > timeout_ms) {
      this->flush_tx();
      return false;
    }
  }
}

void Nrf24Radio::flush_rx() { this->write_command_(FLUSH_RX); }

void Nrf24Radio::flush_tx() { this->write_command_(FLUSH_TX); }

void Nrf24Radio::clear_status_flags() { this->write_register_(STATUS, MASK_RX_DR | MASK_TX_DS | MASK_MAX_RT); }

Nrf24DebugSnapshot Nrf24Radio::read_debug_snapshot() {
  Nrf24DebugSnapshot snapshot;
  snapshot.status = this->get_status_();
  snapshot.config = this->read_register_(CONFIG);
  snapshot.en_aa = this->read_register_(EN_AA);
  snapshot.en_rxaddr = this->read_register_(EN_RXADDR);
  snapshot.setup_aw = this->read_register_(SETUP_AW);
  snapshot.setup_retr = this->read_register_(SETUP_RETR);
  snapshot.observe_tx = this->read_register_(OBSERVE_TX);
  snapshot.rf_ch = this->read_register_(RF_CH);
  snapshot.rf_setup = this->read_register_(RF_SETUP);
  snapshot.feature = this->read_register_(FEATURE);
  snapshot.dynpd = this->read_register_(DYNPD);
  snapshot.fifo_status = this->read_register_(FIFO_STATUS);
  return snapshot;
}

uint8_t Nrf24Radio::get_status_() {
  this->bus_->begin_transaction();
  const uint8_t status = this->bus_->transfer_byte(NOP);
  this->bus_->end_transaction();
  return status;
}

uint8_t Nrf24Radio::read_register_(uint8_t reg) {
  this->bus_->begin_transaction();
  this->bus_->transfer_byte(static_cast<uint8_t>(R_REGISTER | (REGISTER_MASK & reg)));
  const uint8_t value = this->bus_->transfer_byte(0);
  this->bus_->end_transaction();
  return value;
}

void Nrf24Radio::write_register_(uint8_t reg, uint8_t value) {
  this->bus_->begin_transaction();
  this->bus_->transfer_byte(static_cast<uint8_t>(W_REGISTER | (REGISTER_MASK & reg)));
  this->bus_->transfer_byte(value);
  this->bus_->end_transaction();
}

void Nrf24Radio::write_register_(uint8_t reg, const uint8_t *buffer, size_t length) {
  this->bus_->begin_transaction();
  this->bus_->transfer_byte(static_cast<uint8_t>(W_REGISTER | (REGISTER_MASK & reg)));
  this->bus_->write_bytes(buffer, length);
  this->bus_->end_transaction();
}

void Nrf24Radio::toggle_features_() {
  this->bus_->begin_transaction();
  this->bus_->transfer_byte(ACTIVATE);
  this->bus_->transfer_byte(0x73);
  this->bus_->end_transaction();
}

void Nrf24Radio::write_command_(uint8_t command) {
  this->bus_->begin_transaction();
  this->bus_->transfer_byte(command);
  this->bus_->end_transaction();
}

void Nrf24Radio::address_to_bytes_(uint64_t address, uint8_t *out) const {
  for (uint8_t index = 0; index < NRF24_ADDRESS_WIDTH; index++) {
    out[index] = static_cast<uint8_t>((address >> (index * 8U)) & 0xFFU);
  }
}

}  // namespace esphome::harmoino
