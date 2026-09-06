// hardware — full control of board hardware (REQ §7.4, R7). Board knowledge
// lives entirely in devices.yaml; the same binary ships on every platform.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/timestamps.h"
#include "instr/platform/descriptors.h"
#include "instr/platform/platform.h"

namespace instr {
namespace hardware {
namespace domain {

enum class CheckState { Pass, Fail, Degraded, NotPresent, Unknown };
const char* check_state_name(CheckState);

struct CheckResult {
  std::string device;
  std::string check;
  CheckState state = CheckState::Unknown;
  std::string detail;
  uint64_t duration_ms = 0;
  uint64_t mono_ns = 0;
};

class DeviceManager {
 public:
  DeviceManager(platform::IPlatform& plat, TimeSource& ts, const platform::DeviceDescriptor& desc)
      : plat_(plat), ts_(ts), desc_(desc) {}

  const std::vector<platform::DeviceSpec>& devices() const { return desc_.devices; }
  const platform::DeviceSpec* find(const std::string& name) const;

  // ---- control operations (driving devices, not read-only) ----
  Error control(const std::string& device, const std::string& control_op,
                const std::map<std::string, std::string>& args);

  // Read a GPIO line via the platform (read side of the control op).
  Result<bool> gpio_read(const std::string& device);

  // ---- quick health checks: presence + cheap behavioural sanity (one per
  // device; not a full qualification test — that is the IV&V path) ----
  CheckResult run_check(const platform::DeviceSpec& dev, const platform::DeviceCheck& chk);
  std::vector<CheckResult> check_device(const std::string& name);
  std::vector<CheckResult> check_all();

  // Periodic scheduling: returns devices whose periodic check is due.
  void set_periodic_interval_s(double s) { periodic_interval_s_ = s; }
  std::vector<CheckResult> due_checks(uint64_t now_mono_ns);

 private:
  CheckResult make_result(const std::string& dev, const std::string& chk, CheckState st,
                          const std::string& detail, uint64_t started);
  platform::IPlatform& plat_;
  TimeSource& ts_;
  platform::DeviceDescriptor desc_;
  std::map<std::string, uint64_t> last_check_mono_;
  double periodic_interval_s_ = 300;
};

}  // namespace domain
}  // namespace hardware
}  // namespace instr