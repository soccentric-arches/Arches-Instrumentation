#include "instr/hardware/domain.h"

#include "instr/platform/simulated.h"

#include <algorithm>
#include <chrono>

namespace instr {
namespace hardware {
namespace domain {

const char* check_state_name(CheckState s) {
  switch (s) {
    case CheckState::Pass: return "pass";
    case CheckState::Fail: return "fail";
    case CheckState::Degraded: return "degraded";
    case CheckState::NotPresent: return "not-present";
    case CheckState::Unknown: return "unknown";
  }
  return "unknown";
}

const platform::DeviceSpec* DeviceManager::find(const std::string& name) const {
  for (const auto& d : desc_.devices)
    if (d.name == name) return &d;
  return nullptr;
}

CheckResult DeviceManager::make_result(const std::string& dev, const std::string& chk,
                                       CheckState st, const std::string& detail,
                                       uint64_t started) {
  CheckResult r;
  r.device = dev;
  r.check = chk;
  r.state = st;
  r.detail = detail;
  r.mono_ns = ts_.mono_ns();
  r.duration_ms = (ts_.mono_ns() - started) / 1'000'000;
  return r;
}

Error DeviceManager::control(const std::string& device, const std::string& control_op,
                             const std::map<std::string, std::string>& args) {
  const platform::DeviceSpec* dev = find(device);
  if (!dev) return Error::make(Errc::NotFound, "unknown device: " + device, "hardware");
  // Only controls declared in the descriptor are allowed (board knowledge is
  // data, not code).
  bool declared = false;
  for (const auto& c : dev->controls)
    if (c == control_op) declared = true;
  if (!declared)
    return Error::make(Errc::NotSupported,
                       "control op not declared for device " + device + ": " + control_op,
                       device);
  if (dev->kind == "gpio") {
    int line = std::stoi(dev->params.count("line") ? dev->params.at("line") : "-1");
    if (control_op == "gpio_set") {
      auto it = args.find("value");
      if (it == args.end()) return Error::make(Errc::InvalidArgument, "value required", device);
      return plat_.gpio_set(line, it->second == "true" || it->second == "1");
    }
  } else if (dev->kind == "led") {
    if (control_op == "led_set") {
      auto it = args.find("on");
      if (it == args.end()) return Error::make(Errc::InvalidArgument, "on required", device);
      return plat_.led_set(device, it->second == "true" || it->second == "1");
    }
    if (control_op == "led_blink") {
      int on = 100, off = 100, reps = 3;
      auto get = [&](const char* k, int d) {
        auto it = args.find(k);
        return it != args.end() ? std::stoi(it->second) : d;
      };
      on = get("on_ms", on);
      off = get("off_ms", off);
      reps = get("repeats", reps);
      return plat_.led_blink(device, on, off, reps);
    }
  } else if (dev->kind == "usb") {
    if (control_op == "usb_reset") return plat_.usb_reset(dev->params.count("port") ? dev->params.at("port") : device);
  } else if (dev->kind == "display") {
    if (control_op == "display_control") {
      auto it = args.find("cmd");
      if (it == args.end()) return Error::make(Errc::InvalidArgument, "cmd required", device);
      return plat_.display_control(it->second, args.count("arg") ? args.at("arg") : "");
    }
  }
  return Error::make(Errc::NotSupported, "control op not implemented for kind " + dev->kind,
                     control_op);
}

Result<bool> DeviceManager::gpio_read(const std::string& device) {
  const platform::DeviceSpec* dev = find(device);
  if (!dev) return Result<bool>::fail(Errc::NotFound, "unknown device: " + device, "hardware");
  if (dev->kind != "gpio")
    return Result<bool>::fail(Errc::NotSupported, "not a gpio device", device);
  int line = std::stoi(dev->params.count("line") ? dev->params.at("line") : "-1");
  return plat_.gpio_get(line);
}

CheckResult DeviceManager::run_check(const platform::DeviceSpec& dev,
                                     const platform::DeviceCheck& chk) {
  uint64_t started = ts_.mono_ns();
  if (chk.op == "presence") {
    // Cheap presence detection per kind.
    if (dev.kind == "gpio") {
      int line = std::stoi(dev.params.count("line") ? dev.params.at("line") : "-1");
      auto v = plat_.gpio_get(line);
      if (!v.ok()) return make_result(dev.name, chk.name, CheckState::NotPresent, v.error.message, started);
      return make_result(dev.name, chk.name, CheckState::Pass, "line readable", started);
    }
    if (dev.kind == "display") {
      auto p = plat_.display_probe();
      if (!p.ok()) return make_result(dev.name, chk.name, CheckState::NotPresent, p.error.message, started);
      return make_result(dev.name, chk.name, CheckState::Pass, *p, started);
    }
    return make_result(dev.name, chk.name, CheckState::Pass, "assumed present", started);
  }
  if (chk.op == "gpio_toggle_read") {
    int line = std::stoi(dev.params.count("line") ? dev.params.at("line") : "-1");
    // Behavioural sanity: set high, read back; set low, read back.
    Error e1 = plat_.gpio_set(line, true);
    if (!e1.ok()) return make_result(dev.name, chk.name, CheckState::Fail, e1.message, started);
    auto r1 = plat_.gpio_get(line);
    if (!r1.ok() || !*r1)
      return make_result(dev.name, chk.name, CheckState::Degraded, "set-high readback mismatch", started);
    Error e2 = plat_.gpio_set(line, false);
    if (!e2.ok()) return make_result(dev.name, chk.name, CheckState::Fail, e2.message, started);
    auto r2 = plat_.gpio_get(line);
    if (!r2.ok() || *r2)
      return make_result(dev.name, chk.name, CheckState::Degraded, "set-low readback mismatch", started);
    return make_result(dev.name, chk.name, CheckState::Pass, "toggle+read ok", started);
  }
  if (chk.op == "led_blink_ack") {
    Error e = plat_.led_blink(dev.name, 30, 30, 1);
    if (!e.ok()) return make_result(dev.name, chk.name, CheckState::Fail, e.message, started);
    // Blink request acknowledged when the platform records it.
    auto* sim = dynamic_cast<platform::SimulatedPlatform*>(&plat_);
    if (sim) {
      bool ack = sim->led_state().count(dev.name + ":blink-ack") &&
                 sim->led_state().at(dev.name + ":blink-ack");
      return make_result(dev.name, chk.name, ack ? CheckState::Pass : CheckState::Degraded,
                         ack ? "blink acknowledged" : "no blink ack", started);
    }
    return make_result(dev.name, chk.name, CheckState::Pass, "blink issued", started);
  }
  if (chk.op == "can_loopback") {
    std::string iface = dev.params.count("iface") ? dev.params.at("iface") : dev.name;
    auto r = plat_.can_loopback_test(iface);
    if (!r.ok()) return make_result(dev.name, chk.name, CheckState::Fail, r.error.message, started);
    return make_result(dev.name, chk.name, *r ? CheckState::Pass : CheckState::Fail,
                       *r ? "loopback frame ok" : "loopback frame lost", started);
  }
  if (chk.op == "usb_reset_ack") {
    std::string port = dev.params.count("port") ? dev.params.at("port") : dev.name;
    Error e = plat_.usb_reset(port);
    if (!e.ok()) return make_result(dev.name, chk.name, CheckState::Fail, e.message, started);
    return make_result(dev.name, chk.name, CheckState::Pass, "reset accepted", started);
  }
  if (chk.op == "display_probe") {
    auto p = plat_.display_probe();
    if (!p.ok()) return make_result(dev.name, chk.name, CheckState::Fail, p.error.message, started);
    return make_result(dev.name, chk.name, CheckState::Pass, *p, started);
  }
  return make_result(dev.name, chk.name, CheckState::Unknown,
                     "check op not known: " + chk.op, started);
}

std::vector<CheckResult> DeviceManager::check_device(const std::string& name) {
  std::vector<CheckResult> out;
  const platform::DeviceSpec* dev = find(name);
  if (!dev) return out;
  for (const auto& chk : dev->checks) out.push_back(run_check(*dev, chk));
  last_check_mono_[name] = ts_.mono_ns();
  return out;
}

std::vector<CheckResult> DeviceManager::check_all() {
  std::vector<CheckResult> out;
  for (const auto& dev : desc_.devices) {
    auto r = check_device(dev.name);
    out.insert(out.end(), r.begin(), r.end());
  }
  return out;
}

std::vector<CheckResult> DeviceManager::due_checks(uint64_t now_mono_ns) {
  std::vector<CheckResult> out;
  uint64_t interval_ns = uint64_t(periodic_interval_s_ * 1e9);
  for (const auto& dev : desc_.devices) {
    auto it = last_check_mono_.find(dev.name);
    if (it == last_check_mono_.end() || now_mono_ns - it->second >= interval_ns) {
      auto r = check_device(dev.name);
      out.insert(out.end(), r.begin(), r.end());
    }
  }
  return out;
}

}  // namespace domain
}  // namespace hardware
}  // namespace instr