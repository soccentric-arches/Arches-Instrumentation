// Arches Instrumentation — native platform implementation.
// Reads standard Linux interfaces (sysfs/procfs, /dev/watchdog, boot
// counters) that are common across the nine targets; board-specific device
// topology comes from descriptors, never from code (R9). Network/systemd
// mutations are dry-run recorded unless INSTR_APPLY_NETWORK=1 so the same
// binary is safe in CI and on a laptop.
#pragma once
#include "instr/platform/platform.h"

namespace instr {
namespace platform {

class NativePlatform : public IPlatform {
 public:
  explicit NativePlatform(std::string sysroot = "/");

  // Test seam: redirect file reads to a fixture directory.
  void set_sysroot(const std::string& root) { sysroot_ = root; }

  Result<FactoryIdentity> read_factory_identity() override;
  bool entropy_available() override;
  std::vector<StorageDevice> storage_devices() override;
  MemInfo memory_info() override;
  CpuSnapshot cpu_snapshot() override;
  double load_average_1m() override;
  uint32_t process_count() override;
  uint64_t uptime_s() override;
  uint32_t boot_count() override;
  bool last_shutdown_clean() override;
  std::vector<ThermalZone> thermal_zones() override;
  std::vector<PowerSource> power_sources() override;
  Error watchdog_set_timeout_s(int seconds) override;
  Error watchdog_pet() override;
  bool time_locked() override;
  std::vector<IfaceInfo> interfaces() override;
  Result<BootSlotState> boot_slots() override;
  Error set_boot_slot(const std::string& slot, bool trial) override;
  Error confirm_boot_slot() override;
  Error gpio_set(int line, bool value) override;
  Result<bool> gpio_get(int line) override;
  Error led_set(const std::string& name, bool on) override;
  Error led_blink(const std::string& name, int on_ms, int off_ms, int repeats) override;
  Error usb_reset(const std::string& port) override;
  Result<bool> can_loopback_test(const std::string& iface) override;
  Error display_control(const std::string& cmd, const std::string& arg) override;
  Result<std::string> display_probe() override;
  Error unit_start(const std::string& unit) override;
  Error unit_stop(const std::string& unit) override;
  Error unit_restart(const std::string& unit) override;
  Result<bool> unit_active(const std::string& unit) override;
  Result<std::vector<std::string>> kernel_log_lines(bool from_start) override;
  Result<std::vector<std::string>> journal_lines(const std::string& unit, size_t max_lines) override;
  Error apply_network_plan(const std::string& plan_json) override;
  bool hq_reachable() override;
  std::string coredump_dir() override { return "/var/lib/instrumentation/coredumps"; }
  std::string platform_name() override { return "native"; }

 private:
  Result<std::string> read_file(const std::string& path);
  std::vector<std::string> list_dir(const std::string& path);
  bool apply_enabled() const;

  std::string sysroot_;
  int watchdog_fd_ = -1;
  bool watchdog_opened_ = false;
};

}  // namespace platform
}  // namespace instr