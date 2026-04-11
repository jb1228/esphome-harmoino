#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "components/harmoino/harmony_protocol.h"

namespace hc = esphome::harmoino;

struct ExpectedOutput {
  hc::HarmonyOutputKind kind;
  std::string payload;
};

static void expect_payloads(const std::vector<hc::HarmonyOutput> &outputs,
                            const std::vector<std::string> &expected) {
  std::vector<std::string> actual;
  for (const auto &output : outputs) {
    if (output.kind == hc::HarmonyOutputKind::SEMANTIC) {
      actual.push_back(output.payload);
    }
  }

  assert(actual.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) {
    assert(actual[i] == expected[i]);
  }
}

static void expect_physical_outputs(const std::vector<hc::HarmonyOutput> &outputs,
                                    const std::vector<ExpectedOutput> &expected) {
  std::vector<ExpectedOutput> actual;
  for (const auto &output : outputs) {
    if (output.kind != hc::HarmonyOutputKind::SEMANTIC) {
      actual.push_back({output.kind, output.payload});
    }
  }

  assert(actual.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) {
    assert(actual[i].kind == expected[i].kind);
    assert(actual[i].payload == expected[i].payload);
  }
}

static void test_type0_immediate_press() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  auto outputs = decoder.process_command_id(0x005800C1, 0);
  expect_physical_outputs(outputs, {{hc::HarmonyOutputKind::PRESS, "ok"}});
  expect_payloads(outputs, {});
  expect_payloads(decoder.poll(0), {"ok"});
  expect_payloads(decoder.process_command_id(0x000000C1, 40), {});
  expect_physical_outputs(decoder.poll(99), {});
  expect_physical_outputs(decoder.poll(101), {{hc::HarmonyOutputKind::RELEASE, "ok"}});
  expect_payloads(decoder.poll(260), {});
  outputs = decoder.process_command_id(0x005800C1, 300);
  expect_physical_outputs(outputs, {{hc::HarmonyOutputKind::PRESS, "ok"}});
  expect_payloads(outputs, {});
  expect_payloads(decoder.poll(300), {"ok"});
}

static void test_type1_repeat_behavior_with_release_chatter() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_command_id(0x005200C1, 0), {});
  expect_payloads(decoder.poll(0), {"up"});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 100), {});
  expect_payloads(decoder.process_command_id(0x000000C1, 150), {});
  expect_payloads(decoder.process_command_id(0x005200C1, 180), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 400), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 500), {});
  expect_payloads(decoder.poll(599), {});
  expect_payloads(decoder.poll(600), {"up"});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 700), {});
  expect_payloads(decoder.poll(749), {});
  expect_payloads(decoder.poll(750), {"up"});
}

static void test_type2_single_click() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  auto outputs = decoder.process_command_id(0x0001E9C3, 0);
  expect_physical_outputs(outputs, {{hc::HarmonyOutputKind::PRESS, "movie"}});
  expect_payloads(outputs, {});
  expect_payloads(decoder.poll(0), {});
  expect_payloads(decoder.process_command_id(0x000000C3, 50), {});
  expect_physical_outputs(decoder.poll(109), {});
  expect_physical_outputs(decoder.poll(111), {{hc::HarmonyOutputKind::RELEASE, "movie"}});
  expect_payloads(decoder.poll(274), {});
  expect_payloads(decoder.poll(276), {"movie_clicked"});
}

static void test_type2_double_click() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_command_id(0x0001ECC3, 0), {});
  expect_payloads(decoder.process_command_id(0x000000C3, 30), {});
  expect_payloads(decoder.process_command_id(0x0001ECC3, 120), {});
  expect_payloads(decoder.process_command_id(0x000000C3, 160), {});
  expect_payloads(decoder.poll(385), {});
  expect_payloads(decoder.poll(386), {"off_double"});
}

static void test_type2_multiple_click() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_command_id(0x0001EDC3, 0), {});
  expect_payloads(decoder.process_command_id(0x000000C3, 20), {});
  expect_payloads(decoder.process_command_id(0x0001EDC3, 80), {});
  expect_payloads(decoder.process_command_id(0x000000C3, 120), {});
  expect_payloads(decoder.process_command_id(0x0001EDC3, 170), {});
  expect_payloads(decoder.process_command_id(0x000000C3, 210), {});
  expect_payloads(decoder.poll(435), {});
  expect_payloads(decoder.poll(436), {"tv_multiple"});
}

static void test_type2_long_press_from_simple_hold() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_command_id(0x0001E8C3, 10), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 110), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 210), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 310), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_HOLD, 410), {});
  expect_payloads(decoder.poll(560), {"music_long"});
}

static void test_type2_long_press_from_release_chatter_sequence() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54}, 10), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 100), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 108), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D}, 192), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54}, 197), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 299), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D}, 397), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54}, 400), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 439), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 498), {});
  expect_payloads(decoder.poll(560), {"music_long"});
}

static void test_type2_double_click_with_hold_packets() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53}, 0), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 75), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 82), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D}, 181), {});

  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53}, 275), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 286), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 318), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D}, 380), {});

  expect_payloads(decoder.poll(605), {});
  expect_payloads(decoder.poll(606), {"movie_double"});
}

static void test_type2_double_click_ignores_short_repress_chatter() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53}, 0), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 85), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 87), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D}, 133), {});

  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53}, 291), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 315), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 316), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D}, 389), {});

  // The third press is only 36ms after release, so it should be treated as chatter
  // rather than starting a third click or turning the sequence into a long press.
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0xE9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53}, 425), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 487), {});
  expect_payloads(decoder.process_packet({0x00, 0x40, 0x00, 0x28, 0x98}, 509), {});
  expect_payloads(decoder.process_packet({0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D}, 591), {});

  expect_payloads(decoder.poll(816), {});
  expect_payloads(decoder.poll(817), {"movie_double"});
}

static void test_release_markers_do_not_emit_unknown_commands() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  auto outputs = decoder.process_command_id(0x000000C1, 0);
  expect_payloads(outputs, {});
  expect_physical_outputs(outputs, {});
  outputs = decoder.process_command_id(0x000000C3, 10);
  expect_payloads(outputs, {});
  expect_physical_outputs(outputs, {});
  expect_payloads(decoder.poll(250), {});
}

static void test_auxiliary_state_packets_are_ignored() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_command_id(0x002200C1, 0), {});
  expect_payloads(decoder.poll(0), {"number5"});
  expect_payloads(decoder.process_command_id(0x252200C1, 20), {});
  expect_payloads(decoder.poll(20), {});

  decoder.reset();
  decoder.set_command_resolver(&resolver);
  expect_payloads(decoder.process_command_id(0x0001E8C3, 0), {});
  expect_payloads(decoder.process_command_id(0xB401E8C3, 30), {});
  expect_payloads(decoder.process_command_id(0x000000C3, 80), {});
  expect_payloads(decoder.poll(305), {});
  expect_payloads(decoder.poll(306), {"music_clicked"});
}

static void test_explicit_sleep_packet() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_command_id(hc::HARMONY_PING, 0), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_SLEEP, 50), {"sleep"});
}

static void test_inferred_sleep_after_ping_timeout() {
  hc::HarmonyCommandResolver resolver;
  hc::HarmonyDecoder decoder;
  decoder.set_command_resolver(&resolver);

  expect_payloads(decoder.process_command_id(0x005800C1, 0), {});
  expect_payloads(decoder.poll(0), {"ok"});
  expect_payloads(decoder.process_command_id(0x000000C1, 40), {});
  expect_payloads(decoder.process_command_id(hc::HARMONY_PING, 1000), {});
  expect_payloads(decoder.poll(3001), {"sleep"});
}

static void test_command_id_extraction() {
  const std::vector<uint8_t> button_packet = {0x00, 0xC1, 0x00, 0x52, 0x00, 0x00, 0x12};
  const auto button_command = hc::extract_harmony_command_id(button_packet);
  assert(button_command.has_value());
  assert(*button_command == 0x005200C1);

  const std::vector<uint8_t> hold_packet = {0x00, 0x40, 0x00, 0x28, 0x98};
  const auto hold_command = hc::extract_harmony_command_id(hold_packet);
  assert(hold_command.has_value());
  assert(*hold_command == hc::HARMONY_HOLD);
}

static void test_remote_address_extraction() {
  std::vector<uint8_t> payload(22, 0);
  payload[3] = 0xAA;
  payload[4] = 0xBB;
  payload[5] = 0xCC;
  payload[6] = 0xDD;
  payload[7] = 0xEF;

  const auto address = hc::extract_remote_address(payload);
  assert(address.has_value());
  assert(*address == 0xAABBCCDDEEULL);
}

static void test_effective_address_precedence() {
  assert(hc::select_effective_address(0x1111111111ULL, std::nullopt) == 0x1111111111ULL);
  assert(hc::select_effective_address(0x1111111111ULL, 0x2222222222ULL) == 0x1111111111ULL);
  assert(hc::select_effective_address(std::nullopt, 0x2222222222ULL) == 0x2222222222ULL);
  assert(!hc::select_effective_address(std::nullopt, std::nullopt).has_value());
  assert(hc::format_address_text(0xAABBCCDDEEULL) == "0xAABBCCDDEE");
  assert(hc::format_address_text(std::nullopt).empty());
}

static void test_packet_descriptions() {
  hc::HarmonyCommandResolver resolver;
  assert(hc::describe_harmony_packet({0x00, 0x40, 0x00, 0x28, 0x98}, &resolver) == "hold");
  assert(hc::describe_harmony_packet({0x00, 0x40, 0x04, 0x4C, 0x70}, &resolver) == "ping");
  assert(hc::describe_harmony_packet({0x00, 0x00, 0x00, 0x00, 0x00}, &resolver).empty());
}

int main() {
  test_type0_immediate_press();
  test_type1_repeat_behavior_with_release_chatter();
  test_type2_single_click();
  test_type2_double_click();
  test_type2_multiple_click();
  test_type2_long_press_from_simple_hold();
  test_type2_long_press_from_release_chatter_sequence();
  test_type2_double_click_with_hold_packets();
  test_type2_double_click_ignores_short_repress_chatter();
  test_release_markers_do_not_emit_unknown_commands();
  test_auxiliary_state_packets_are_ignored();
  test_explicit_sleep_packet();
  test_inferred_sleep_after_ping_timeout();
  test_command_id_extraction();
  test_remote_address_extraction();
  test_effective_address_precedence();
  test_packet_descriptions();
  std::cout << "All harmoino protocol tests passed\n";
  return 0;
}
