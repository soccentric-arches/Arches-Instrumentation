// Unit tests — hardware domain (REQ §12).
#include <gtest/gtest.h>

#include "instr/common/timestamps.h"
#include "instr/hardware/domain.h"
#include "instr/platform/simulated.h"

using namespace instr;
using namespace instr::hardware;
using namespace instr::hardware::domain;

static platform::DeviceDescriptor test_descriptor() {
  platform::DeviceDescriptor d;
  d.board = "sim-board";
  {
    platform::DeviceSpec s;
    s.name = "status-led";
    s.kind = "led";
    s.controls = {"led_set", "led_blink"};
    s.checks = {{"blink-ack", "led_blink_ack", {}}, {"presence", "presence", {}}};
    d.devices.push_back(s);
  }
  {
    platform::DeviceSpec s;
    s.name = "gpio-1";
    s.kind = "gpio";
    s.params["line"] = "4";
    s.controls = {"gpio_set"};
    s.checks = {{"toggle", "gpio_toggle_read", {}}};
    d.devices.push_back(s);
  }
  {
    platform::DeviceSpec s;
    s.name = "can0";
    s.kind = "can";
    s.params["iface"] = "can0";
    s.checks = {{"loopback", "can_loopback", {}}};
    d.devices.push_back(s);
  }
  {
    platform::DeviceSpec s;
    s.name = "display";
    s.kind = "display";
    s.controls = {"display_control"};
    s.checks = {{"presence", "presence", {}}, {"probe", "display_probe", {}}};
    d.devices.push_back(s);
  }
  return d;
}

TEST(HardwareChecks, GpioToggleReadPasses) {
  platform::SimulatedPlatform plat;
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  auto r = mgr.check_device("gpio-1");
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0].state, CheckState::Pass);
  EXPECT_NE(r[0].detail.find("toggle+read ok"), std::string::npos);
}

TEST(HardwareChecks, GpioStuckFails) {
  platform::SimulatedPlatform plat;
  plat.fail_gpio(true);
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  auto r = mgr.check_device("gpio-1");
  EXPECT_EQ(r[0].state, CheckState::Fail);
}

TEST(HardwareChecks, CanLoopbackFailsWhenInjected) {
  platform::SimulatedPlatform plat;
  plat.fail_can(true);
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  auto r = mgr.check_device("can0");
  EXPECT_EQ(r[0].state, CheckState::Fail);
}

TEST(HardwareChecks, DisplayNotPresent) {
  platform::SimulatedPlatform plat;
  plat.fail_display(true);
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  auto r = mgr.check_device("display");
  // presence probe fails → not-present; probe check fails
  bool saw_not_present = false, saw_fail = false;
  for (const auto& c : r) {
    if (c.state == CheckState::NotPresent) saw_not_present = true;
    if (c.state == CheckState::Fail) saw_fail = true;
  }
  EXPECT_TRUE(saw_not_present);
  EXPECT_TRUE(saw_fail);
}

TEST(HardwareControls, DescriptorGatesControls) {
  platform::SimulatedPlatform plat;
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  // gpio-1 declares gpio_set only — led_set is rejected.
  std::map<std::string, std::string> m;
  m["on"] = "true";
  EXPECT_FALSE(mgr.control("gpio-1", "led_set", m).ok());
  // Declared control works.
  m.clear();
  m["value"] = "true";
  EXPECT_TRUE(mgr.control("gpio-1", "gpio_set", m).ok());
  auto v = mgr.gpio_read("gpio-1");
  ASSERT_TRUE(v.ok());
  EXPECT_TRUE(*v);
}

TEST(HardwareControls, LedBlinkAcknowledged) {
  platform::SimulatedPlatform plat;
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  std::map<std::string, std::string> m;
  m["repeats"] = "3";
  EXPECT_TRUE(mgr.control("status-led", "led_blink", m).ok());
  auto* sim = dynamic_cast<platform::SimulatedPlatform*>(&plat);
  ASSERT_NE(sim, nullptr);
  EXPECT_TRUE(sim->led_state().at("status-led:blink-ack"));
}

TEST(HardwareControls, UnknownDeviceIsLoudError) {
  platform::SimulatedPlatform plat;
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  auto e = mgr.control("nonexistent", "led_set", {});
  EXPECT_EQ(e.code, Errc::NotFound);
  EXPECT_NE(e.message.find("nonexistent"), std::string::npos);
}

TEST(HardwareSchedule, DueChecksRunOncePerInterval) {
  platform::SimulatedPlatform plat;
  ManualTimeSource ts;
  DeviceManager mgr(plat, ts, test_descriptor());
  mgr.set_periodic_interval_s(10);
  auto first = mgr.due_checks(ts.mono_ns());
  EXPECT_FALSE(first.empty());
  auto soon = mgr.due_checks(ts.mono_ns() + 1'000'000ull);  // 1 ms later
  EXPECT_TRUE(soon.empty());
  ts.mono += 11ull * 1'000'000'000ull;
  auto later = mgr.due_checks(ts.mono_ns());
  EXPECT_FALSE(later.empty());
}