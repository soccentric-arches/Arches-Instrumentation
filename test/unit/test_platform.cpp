// Unit tests — simulated platform and descriptor loading (REQ §5, §12).
#include <gtest/gtest.h>

#include "instr/common/config.h"
#include "instr/common/json.h"
#include "instr/platform/descriptors.h"
#include "instr/platform/simulated.h"

using namespace instr;
using namespace instr::platform;

TEST(SimulatedPlatform, FactoryIdentityAndEntropy) {
  SimulatedPlatform plat;
  auto id = plat.read_factory_identity();
  ASSERT_TRUE(id.ok());
  EXPECT_EQ(id->serial, "SIM-0001");
  EXPECT_TRUE(plat.entropy_available());
  plat.set_entropy_available(false);
  EXPECT_FALSE(plat.entropy_available());
}

TEST(SimulatedPlatform, BootSlotsTrialAndConfirm) {
  SimulatedPlatform plat;
  auto s = plat.boot_slots();
  ASSERT_TRUE(s.ok());
  EXPECT_EQ(s->active, "A");
  EXPECT_TRUE(plat.set_boot_slot("B", true).ok());
  s = plat.boot_slots();
  EXPECT_EQ(s->pending, "B");
  EXPECT_EQ(s->active, "A");  // trial boot: not yet confirmed
  EXPECT_TRUE(plat.confirm_boot_slot().ok());
  s = plat.boot_slots();
  EXPECT_EQ(s->active, "B");
  EXPECT_EQ(s->inactive, "A");
}

TEST(SimulatedPlatform, WatchdogPetCounts) {
  SimulatedPlatform plat;
  EXPECT_TRUE(plat.watchdog_set_timeout_s(30).ok());
  EXPECT_TRUE(plat.watchdog_pet().ok());
  EXPECT_TRUE(plat.watchdog_pet().ok());
  EXPECT_EQ(plat.watchdog_pets(), 2u);
  plat.fail_watchdog(true);
  EXPECT_FALSE(plat.watchdog_pet().ok());
}

TEST(SimulatedPlatform, HardwareControlObservation) {
  SimulatedPlatform plat;
  EXPECT_TRUE(plat.gpio_set(4, true).ok());
  auto v = plat.gpio_get(4);
  ASSERT_TRUE(v.ok());
  EXPECT_TRUE(*v);
  EXPECT_TRUE(plat.led_blink("status", 10, 10, 3).ok());
  EXPECT_TRUE(plat.led_state().at("status:blink-ack"));
  EXPECT_TRUE(plat.usb_reset("1-2").ok());
  EXPECT_EQ(plat.usb_resets().size(), 1u);
  EXPECT_TRUE(plat.display_control("blank", "").ok());
  EXPECT_EQ(plat.display_commands().size(), 1u);
}

TEST(SimulatedPlatform, UnitOrchestrationObservation) {
  SimulatedPlatform plat;
  EXPECT_TRUE(plat.unit_start("instr-alarms.service").ok());
  auto active = plat.unit_active("instr-alarms.service");
  ASSERT_TRUE(active.ok());
  EXPECT_TRUE(*active);
  EXPECT_TRUE(plat.unit_restart("instr-alarms.service").ok());
  EXPECT_EQ(plat.unit_restarts().size(), 1u);
}

TEST(Descriptors, DeviceDescriptorLoadAndValidate) {
  auto schema = load_schema_text(R"({"type":"object","properties":{
    "board":{"type":"string"},
    "devices":{"type":"array","items":{"type":"object","properties":{
      "name":{"type":"string"},"kind":{"type":"string"},
      "controls":{"type":"array","items":{"type":"string"}},
      "checks":{"type":"array","items":{"type":"object","properties":{
        "name":{"type":"string"},"op":{"type":"string"}},"required":["name","op"]}},
      "params":{"type":"object"}},"required":["name","kind"]}}}})");
  ASSERT_TRUE(schema.ok());
  const char* yaml = R"yaml(
board: sim-board
devices:
  - name: status-led
    kind: led
    controls: [led_set, led_blink]
    checks:
      - name: blink-ack
        op: led_blink_ack
  - name: gpio-1
    kind: gpio
    params:
      line: 4
    controls: [gpio_set]
    checks:
      - name: toggle
        op: gpio_toggle_read
)yaml";
  auto d = load_device_descriptor(yaml, *schema, "devices.yaml");
  ASSERT_TRUE(d.ok());
  EXPECT_EQ(d->board, "sim-board");
  ASSERT_EQ(d->devices.size(), 2u);
  EXPECT_EQ(d->devices[1].params.at("line"), "4");
  EXPECT_EQ(d->devices[0].checks[0].op, "led_blink_ack");

  // Reject with a diagnosable error on schema violation.
  auto bad = load_device_descriptor("devices:\n  - name: x\n", *schema, "devices.yaml");
  EXPECT_FALSE(bad.ok());
  EXPECT_NE(bad.error.message.find("devices.yaml"), std::string::npos);
  EXPECT_NE(bad.error.message.find("kind"), std::string::npos);
}

TEST(Descriptors, ResourceProfileLoad) {
  auto schema = load_schema_text(R"({"type":"object","properties":{
    "platform_class":{"type":"string","enum":["small","medium","large"]},
    "sample_interval_default_s":{"type":"number"},
    "spool_max_bytes":{"type":"integer"},
    "sample_intervals_per_class_s":{"type":"object"}}})");
  ASSERT_TRUE(schema.ok());
  const char* yaml = R"yaml(
platform_class: small
sample_interval_default_s: 10
sample_intervals_per_class_s:
  thermal: 2
  cpu: 5
spool_max_bytes: 33554432
)yaml";
  auto p = load_resource_profile(yaml, *schema, "resource-profile.yaml");
  ASSERT_TRUE(p.ok());
  EXPECT_EQ(p->platform_class, "small");
  EXPECT_DOUBLE_EQ(p->sample_intervals_per_class_s.at("thermal"), 2.0);
  EXPECT_EQ(p->spool_max_bytes, 33554432u);
}

TEST(Descriptors, CapabilityManifest) {
  auto schema = load_schema_text(R"({"type":"object","properties":{
    "board":{"type":"string"},"capabilities":{"type":"array","items":{"type":"string"}}}})");
  auto m = load_capability_manifest("board: jetson-orin\ncapabilities: [gnss, ptp, can, display]\n",
                                    *schema, "caps.yaml");
  ASSERT_TRUE(m.ok());
  EXPECT_TRUE(m->has("gnss"));
  EXPECT_FALSE(m->has("watchdog"));
}
