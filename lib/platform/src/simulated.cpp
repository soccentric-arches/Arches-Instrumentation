#include "instr/platform/simulated.h"

namespace instr {
namespace platform {

SimulatedPlatform::SimulatedPlatform() {
  identity_.serial = "SIM-0001";
  identity_.hw_revision = "rev-a";
  identity_.board_variant = "sim";
  identity_.mfg_date = "2025-01-01";
  identity_.hostname_base = "sim-device";
  identity_.identity_secret = "sim-factory-secret";
  identity_.valid = true;
  slots_.active = "A";
  slots_.inactive = "B";
  slots_.golden = "A";
  units_["instr-istate.service"] = true;
}

void SimulatedPlatform::set_factory_identity(const FactoryIdentity& id) {
  std::lock_guard<std::mutex> lk(mu_);
  identity_ = id;
}
void SimulatedPlatform::inject_storage_device(const StorageDevice& d) {
  std::lock_guard<std::mutex> lk(mu_);
  storage_.push_back(d);
}
void SimulatedPlatform::set_thermal(const std::string& id, const std::string& label, double c) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& t : thermals_)
    if (t.id == id) { t.temp_c = c; t.label = label; return; }
  thermals_.push_back(ThermalZone{id, label, c});
}
void SimulatedPlatform::set_power(const std::string& id, const std::string& kind, double pct,
                                  bool charging) {
  std::lock_guard<std::mutex> lk(mu_);
  power_.push_back(PowerSource{id, kind, pct, charging});
}
void SimulatedPlatform::inject_iface(const IfaceInfo& i) {
  std::lock_guard<std::mutex> lk(mu_);
  ifaces_.push_back(i);
}
void SimulatedPlatform::append_kernel_line(const std::string& line) {
  std::lock_guard<std::mutex> lk(mu_);
  kernel_log_.push_back(line);
}

Result<FactoryIdentity> SimulatedPlatform::read_factory_identity() {
  std::lock_guard<std::mutex> lk(mu_);
  if (!identity_.valid)
    return Result<FactoryIdentity>::fail(Errc::NotFound, "factory identity not provisioned",
                                         "simulated");
  return Result<FactoryIdentity>::good(identity_);
}
bool SimulatedPlatform::entropy_available() { return entropy_; }
std::vector<StorageDevice> SimulatedPlatform::storage_devices() {
  std::lock_guard<std::mutex> lk(mu_);
  return storage_;
}
MemInfo SimulatedPlatform::memory_info() {
  std::lock_guard<std::mutex> lk(mu_);
  return mem_;
}
CpuSnapshot SimulatedPlatform::cpu_snapshot() {
  std::lock_guard<std::mutex> lk(mu_);
  return cpu_;
}
double SimulatedPlatform::load_average_1m() { return load_; }
uint32_t SimulatedPlatform::process_count() { return processes_; }
uint64_t SimulatedPlatform::uptime_s() { return uptime_; }
uint32_t SimulatedPlatform::boot_count() { return boot_count_; }
bool SimulatedPlatform::last_shutdown_clean() { return clean_shutdown_; }
std::vector<ThermalZone> SimulatedPlatform::thermal_zones() {
  std::lock_guard<std::mutex> lk(mu_);
  return thermals_;
}
std::vector<PowerSource> SimulatedPlatform::power_sources() {
  std::lock_guard<std::mutex> lk(mu_);
  return power_;
}
Error SimulatedPlatform::watchdog_set_timeout_s(int seconds) {
  (void)seconds;
  if (fail_watchdog_) return Error::make(Errc::Io, "simulated watchdog open failure", "watchdog");
  watchdog_running_ = true;
  return Error::none();
}
Error SimulatedPlatform::watchdog_pet() {
  if (fail_watchdog_) return Error::make(Errc::Io, "simulated watchdog pet failure", "watchdog");
  ++watchdog_pets_;
  return Error::none();
}
bool SimulatedPlatform::time_locked() { return time_locked_; }
std::vector<IfaceInfo> SimulatedPlatform::interfaces() {
  std::lock_guard<std::mutex> lk(mu_);
  return ifaces_;
}
Result<BootSlotState> SimulatedPlatform::boot_slots() {
  if (fail_boot_slots_) return Result<BootSlotState>::fail(Errc::Io, "simulated bootctl failure", "boot");
  std::lock_guard<std::mutex> lk(mu_);
  return Result<BootSlotState>::good(slots_);
}
Error SimulatedPlatform::set_boot_slot(const std::string& slot, bool trial) {
  if (fail_boot_slots_) return Error::make(Errc::Io, "simulated set-slot failure", "boot");
  std::lock_guard<std::mutex> lk(mu_);
  if (slot != "A" && slot != "B") return Error::make(Errc::InvalidArgument, "bad slot", slot);
  if (trial) slots_.pending = slot;
  return Error::none();
}
Error SimulatedPlatform::confirm_boot_slot() {
  if (fail_boot_slots_) return Error::make(Errc::Io, "simulated confirm failure", "boot");
  std::lock_guard<std::mutex> lk(mu_);
  if (!slots_.pending.empty()) {
    slots_.inactive = slots_.active;
    slots_.active = slots_.pending;
    slots_.pending.clear();
  }
  return Error::none();
}
Error SimulatedPlatform::gpio_set(int line, bool value) {
  if (fail_gpio_) return Error::make(Errc::Io, "simulated gpio failure", "gpio");
  std::lock_guard<std::mutex> lk(mu_);
  gpios_[line] = value;
  return Error::none();
}
Result<bool> SimulatedPlatform::gpio_get(int line) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = gpios_.find(line);
  if (it == gpios_.end()) return Result<bool>::fail(Errc::NotFound, "gpio not configured", "gpio");
  return Result<bool>::good(it->second);
}
Error SimulatedPlatform::led_set(const std::string& name, bool on) {
  std::lock_guard<std::mutex> lk(mu_);
  leds_[name] = on;
  return Error::none();
}
Error SimulatedPlatform::led_blink(const std::string& name, int on_ms, int off_ms, int repeats) {
  std::lock_guard<std::mutex> lk(mu_);
  (void)on_ms; (void)off_ms;
  // Behavioural sanity: acknowledge the blink request.
  leds_[name + ":blink-ack"] = repeats > 0;
  return Error::none();
}
Error SimulatedPlatform::usb_reset(const std::string& port) {
  if (fail_usb_) return Error::make(Errc::Io, "simulated usb reset failure", port);
  std::lock_guard<std::mutex> lk(mu_);
  usb_resets_.push_back(port);
  return Error::none();
}
Result<bool> SimulatedPlatform::can_loopback_test(const std::string& iface) {
  if (fail_can_) return Result<bool>::good(false);
  (void)iface;
  return Result<bool>::good(true);
}
Error SimulatedPlatform::display_control(const std::string& cmd, const std::string& arg) {
  if (fail_display_) return Error::make(Errc::Io, "simulated display failure", cmd);
  std::lock_guard<std::mutex> lk(mu_);
  display_cmds_.push_back(cmd + ":" + arg);
  return Error::none();
}
Result<std::string> SimulatedPlatform::display_probe() {
  if (fail_display_) return Result<std::string>::fail(Errc::Io, "simulated display probe failure", "display");
  return Result<std::string>::good("ok 1280x800");
}
Error SimulatedPlatform::unit_start(const std::string& unit) {
  if (fail_unit_start_) return Error::make(Errc::Unavailable, "simulated systemctl failure", unit);
  std::lock_guard<std::mutex> lk(mu_);
  units_[unit] = true;
  started_units_.push_back(unit);
  return Error::none();
}
Error SimulatedPlatform::unit_stop(const std::string& unit) {
  std::lock_guard<std::mutex> lk(mu_);
  units_[unit] = false;
  return Error::none();
}
Error SimulatedPlatform::unit_restart(const std::string& unit) {
  std::lock_guard<std::mutex> lk(mu_);
  units_[unit] = true;
  restarts_.push_back(unit);
  return Error::none();
}
Result<bool> SimulatedPlatform::unit_active(const std::string& unit) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = units_.find(unit);
  return Result<bool>::good(it != units_.end() && it->second);
}
Result<std::vector<std::string>> SimulatedPlatform::kernel_log_lines(bool from_start) {
  std::lock_guard<std::mutex> lk(mu_);
  if (from_start) return Result<std::vector<std::string>>::good(kernel_log_);
  // Follow mode: return the tail (last 100 lines).
  size_t n = kernel_log_.size();
  size_t start = n > 100 ? n - 100 : 0;
  return Result<std::vector<std::string>>::good(
      std::vector<std::string>(kernel_log_.begin() + long(start), kernel_log_.end()));
}
Result<std::vector<std::string>> SimulatedPlatform::journal_lines(const std::string& unit,
                                                                  size_t max_lines) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = journal_.find(unit);
  if (it == journal_.end()) return Result<std::vector<std::string>>::good({});
  std::vector<std::string> out = it->second;
  if (out.size() > max_lines) out.erase(out.begin(), out.end() - long(max_lines));
  return Result<std::vector<std::string>>::good(std::move(out));
}
Error SimulatedPlatform::apply_network_plan(const std::string& plan_json) {
  std::lock_guard<std::mutex> lk(mu_);
  applied_plans_.push_back(plan_json);
  return Error::none();
}
bool SimulatedPlatform::hq_reachable() { return hq_reachable_; }

}  // namespace platform
}  // namespace instr