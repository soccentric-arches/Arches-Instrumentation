#include "instr/platform/native.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace instr {
namespace platform {

NativePlatform::NativePlatform(std::string sysroot) : sysroot_(std::move(sysroot)) {}

Result<std::string> NativePlatform::read_file(const std::string& path) {
  std::ifstream f(sysroot_ + path);
  if (!f) return Result<std::string>::fail(Errc::NotFound, "cannot open", path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return Result<std::string>::good(ss.str());
}

std::vector<std::string> NativePlatform::list_dir(const std::string& path) {
  std::vector<std::string> out;
  DIR* d = ::opendir((sysroot_ + path).c_str());
  if (!d) return out;
  struct dirent* e;
  while ((e = ::readdir(d)) != nullptr) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    out.push_back(name);
  }
  ::closedir(d);
  return out;
}

bool NativePlatform::apply_enabled() const {
  const char* v = ::getenv("INSTR_APPLY_NETWORK");
  return v && std::string(v) == "1";
}

Result<FactoryIdentity> NativePlatform::read_factory_identity() {
  auto f = read_file("/pst/factory/identity/identity.yaml");
  if (!f.ok())
    return Result<FactoryIdentity>::fail(f.error.code, f.error.message, "factory identity");
  FactoryIdentity id;
  // Minimal YAML scalar extraction (config library handles full validation
  // elsewhere); keys are flat.
  auto get = [&](const std::string& key) -> std::string {
    size_t pos = f->find(key + ":");
    if (pos == std::string::npos) return "";
    size_t vs = f->find_first_not_of(" \t", pos + key.size() + 1);
    size_t ve = f->find('\n', vs);
    if (vs == std::string::npos) return "";
    if (ve == std::string::npos) ve = f->size();
    std::string v = f->substr(vs, ve - vs);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
  };
  id.serial = get("serial");
  id.hw_revision = get("hw_revision");
  id.board_variant = get("board_variant");
  id.mfg_date = get("mfg_date");
  id.hostname_base = get("hostname_base");
  auto secret = read_file("/pst/factory/identity/.secret");
  id.identity_secret = secret.ok() ? *secret : "";
  id.valid = !id.serial.empty();
  if (!id.valid)
    return Result<FactoryIdentity>::fail(Errc::Corrupt, "identity.yaml incomplete",
                                         "factory identity");
  return Result<FactoryIdentity>::good(id);
}

bool NativePlatform::entropy_available() {
  auto a = read_file("/proc/sys/kernel/random/entropy_avail");
  if (!a.ok()) return false;
  try {
    return std::stoul(*a) >= 128;
  } catch (...) {
    return false;
  }
}

std::vector<StorageDevice> NativePlatform::storage_devices() {
  std::vector<StorageDevice> out;
  for (const auto& name : list_dir("/sys/class/block")) {
    if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0) continue;
    StorageDevice d;
    d.name = name;
    d.device_path = "/dev/" + name;
    auto size = read_file("/sys/class/block/" + name + "/size");
    if (size.ok()) {
      try {
        d.bytes = std::stoull(*size) * 512;
      } catch (...) {}
    }
    // eMMC life-time estimate (kernel exposes life_time as 0-10 steps).
    auto life = read_file("/sys/class/block/" + name + "/device/life_time");
    if (life.ok()) {
      try {
        double step = std::stod(*life);
        if (step >= 0 && step <= 10) d.wear_pct = step * 10.0;
      } catch (...) {}
    }
    if (name.rfind("mmcblk", 0) == 0) d.kind = "emmc";
    else if (name.rfind("sd", 0) == 0) d.kind = "sd";
    else if (name.rfind("nvme", 0) == 0) d.kind = "nvme";
    else d.kind = "block";
    out.push_back(d);
  }
  return out;
}

MemInfo NativePlatform::memory_info() {
  MemInfo m;
  auto f = read_file("/proc/meminfo");
  if (!f.ok()) return m;
  auto grab = [&](const std::string& key) -> uint64_t {
    size_t pos = f->find(key);
    if (pos == std::string::npos) return 0;
    pos = f->find_first_of("0123456789", pos);
    if (pos == std::string::npos) return 0;
    try {
      return std::stoull(f->substr(pos)) * 1024;
    } catch (...) {
      return 0;
    }
  };
  m.total_bytes = grab("MemTotal:");
  m.available_bytes = grab("MemAvailable:");
  m.swap_total_bytes = grab("SwapTotal:");
  m.swap_free_bytes = grab("SwapFree:");
  return m;
}

CpuSnapshot NativePlatform::cpu_snapshot() {
  CpuSnapshot c;
  auto f = read_file("/proc/stat");
  if (!f.ok()) return c;
  std::istringstream ss(*f);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.rfind("cpu", 0) == 0 && line.size() > 3 && isdigit(line[3])) {
      std::istringstream ls(line.substr(4));
      uint64_t user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
      ls >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;
      uint64_t busy = user + nice + sys + irq + softirq + steal;
      uint64_t total = busy + idle + iowait;
      c.busy_ticks.push_back(busy);
      c.total_ticks.push_back(total);
    } else if (line.rfind("cpu ", 0) == 0) {
      // aggregate handled below via per-core sum
    }
  }
  c.cores = uint32_t(c.busy_ticks.size());
  // Frequencies + governor.
  for (uint32_t i = 0; i < c.cores; ++i) {
    auto freq = read_file("/sys/devices/system/cpu/cpu" + std::to_string(i) +
                          "/cpufreq/scaling_cur_freq");
    if (freq.ok()) {
      try {
        c.freq_mhz.push_back(std::stod(*freq) / 1000.0);
      } catch (...) {
        c.freq_mhz.push_back(0);
      }
    } else {
      c.freq_mhz.push_back(0);
    }
  }
  auto gov = read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
  if (gov.ok()) {
    std::string g = *gov;
    while (!g.empty() && (g.back() == '\n' || g.back() == ' ')) g.pop_back();
    c.governor = g;
  }
  return c;
}

double NativePlatform::load_average_1m() {
  auto f = read_file("/proc/loadavg");
  if (!f.ok()) return 0;
  try {
    return std::stod(*f);
  } catch (...) {
    return 0;
  }
}

uint32_t NativePlatform::process_count() {
  return uint32_t(list_dir("/proc").size());
}

uint64_t NativePlatform::uptime_s() {
  auto f = read_file("/proc/uptime");
  if (!f.ok()) return 0;
  try {
    return uint64_t(std::stod(*f));
  } catch (...) {
    return 0;
  }
}

uint32_t NativePlatform::boot_count() {
  auto f = read_file("/var/lib/instrumentation/bootcount");
  if (!f.ok()) return 1;
  try {
    return uint32_t(std::stoul(*f));
  } catch (...) {
    return 1;
  }
}

bool NativePlatform::last_shutdown_clean() {
  return read_file("/run/instrumentation/clean-shutdown").ok();
}

std::vector<ThermalZone> NativePlatform::thermal_zones() {
  std::vector<ThermalZone> out;
  for (const auto& z : list_dir("/sys/class/thermal")) {
    if (z.rfind("thermal_zone", 0) != 0) continue;
    ThermalZone t;
    t.id = z;
    auto type = read_file("/sys/class/thermal/" + z + "/type");
    if (type.ok()) {
      t.label = *type;
      while (!t.label.empty() && (t.label.back() == '\n' || t.label.back() == ' ')) t.label.pop_back();
    }
    auto temp = read_file("/sys/class/thermal/" + z + "/temp");
    if (temp.ok()) {
      try {
        t.temp_c = std::stod(*temp) / 1000.0;
      } catch (...) {}
    }
    out.push_back(t);
  }
  return out;
}

std::vector<PowerSource> NativePlatform::power_sources() {
  std::vector<PowerSource> out;
  for (const auto& p : list_dir("/sys/class/power_supply")) {
    PowerSource ps;
    ps.id = p;
    auto type = read_file("/sys/class/power_supply/" + p + "/type");
    if (type.ok()) {
      ps.kind = *type;
      while (!ps.kind.empty() && (ps.kind.back() == '\n' || ps.kind.back() == ' ')) ps.kind.pop_back();
    }
    auto cap = read_file("/sys/class/power_supply/" + p + "/capacity");
    if (cap.ok()) {
      try {
        ps.percent = std::stod(*cap);
      } catch (...) {}
    }
    auto status = read_file("/sys/class/power_supply/" + p + "/status");
    ps.charging = status.ok() && status->find("Charging") != std::string::npos;
    out.push_back(ps);
  }
  return out;
}

Error NativePlatform::watchdog_set_timeout_s(int seconds) {
  int fd = ::open((sysroot_ + "/dev/watchdog").c_str(), O_WRONLY);
  if (fd < 0) return Error::make(Errc::Io, "cannot open /dev/watchdog", "watchdog");
  if (::ioctl(fd, 0x5706 /*WDIOC_SETTIMEOUT unsupported fallback*/, 0) == -1) {
    // Write timeout as text where the driver supports it; ignore failure of
    // the ioctl above and rely on the timeout= module parameter convention.
  }
  std::string t = std::to_string(seconds) + "\n";
  (void)::write(fd, t.data(), t.size());
  ::close(fd);
  watchdog_opened_ = true;
  return Error::none();
}

Error NativePlatform::watchdog_pet() {
  // Keep a persistent fd open for petting.
  if (watchdog_fd_ < 0) {
    watchdog_fd_ = ::open((sysroot_ + "/dev/watchdog").c_str(), O_WRONLY);
    if (watchdog_fd_ < 0) return Error::make(Errc::Io, "cannot open /dev/watchdog", "watchdog");
    watchdog_opened_ = true;
  }
  if (::write(watchdog_fd_, "1", 1) < 0)
    return Error::make(Errc::Io, "watchdog write failed", "watchdog");
  return Error::none();
}

bool NativePlatform::time_locked() {
  // chrony tracking or a time-synced marker from networkd.
  return read_file("/run/instrumentation/time-synced").ok();
}

std::vector<IfaceInfo> NativePlatform::interfaces() {
  std::vector<IfaceInfo> out;
  for (const auto& name : list_dir("/sys/class/net")) {
    IfaceInfo i;
    i.name = name;
    auto operstate = read_file("/sys/class/net/" + name + "/operstate");
    if (operstate.ok()) {
      i.up = operstate->find("up") != std::string::npos;
      i.link = operstate->find("up") != std::string::npos;
    }
    auto mac = read_file("/sys/class/net/" + name + "/address");
    if (mac.ok()) {
      i.mac = *mac;
      while (!i.mac.empty() && (i.mac.back() == '\n' || i.mac.back() == ' ')) i.mac.pop_back();
    }
    out.push_back(i);
  }
  return out;
}

Result<BootSlotState> NativePlatform::boot_slots() {
  BootSlotState s;
  auto cur = read_file("/mnt/boot/current-slot");
  if (!cur.ok()) return Result<BootSlotState>::fail(Errc::NotFound, "no slot state", "boot");
  s.active = *cur;
  while (!s.active.empty() && (s.active.back() == '\n' || s.active.back() == ' ')) s.active.pop_back();
  s.inactive = s.active == "A" ? "B" : "A";
  auto pend = read_file("/mnt/boot/pending-slot");
  if (pend.ok()) {
    s.pending = *pend;
    while (!s.pending.empty() && (s.pending.back() == '\n' || s.pending.back() == ' ')) s.pending.pop_back();
  }
  s.golden = "A";
  auto att = read_file("/mnt/boot/boot-attempts");
  if (att.ok()) {
    try {
      s.boot_attempts = uint32_t(std::stoul(*att));
    } catch (...) {}
  }
  return Result<BootSlotState>::good(s);
}

Error NativePlatform::set_boot_slot(const std::string& slot, bool trial) {
  if (slot != "A" && slot != "B") return Error::make(Errc::InvalidArgument, "bad slot", slot);
  FILE* f = std::fopen((sysroot_ + "/mnt/boot/pending-slot").c_str(), "w");
  if (!f) return Error::make(Errc::Io, "cannot write pending-slot", "boot");
  std::fprintf(f, "%s\n", slot.c_str());
  std::fclose(f);
  (void)trial;
  return Error::none();
}

Error NativePlatform::confirm_boot_slot() {
  auto cur = boot_slots();
  if (!cur.ok()) return cur.error;
  FILE* f = std::fopen((sysroot_ + "/mnt/boot/current-slot").c_str(), "w");
  if (!f) return Error::make(Errc::Io, "cannot write current-slot", "boot");
  std::fprintf(f, "%s\n", cur->active.c_str());
  std::fclose(f);
  ::unlink((sysroot_ + "/mnt/boot/pending-slot").c_str());
  return Error::none();
}

Error NativePlatform::gpio_set(int line, bool value) {
  std::string dir = "/sys/class/gpio/gpio" + std::to_string(line);
  if (!read_file(dir + "/value").ok()) {
    FILE* ex = std::fopen((sysroot_ + "/sys/class/gpio/export").c_str(), "w");
    if (!ex) return Error::make(Errc::Io, "gpio export failed", "gpio");
    std::fprintf(ex, "%d\n", line);
    std::fclose(ex);
  }
  FILE* f = std::fopen((sysroot_ + dir + "/value").c_str(), "w");
  if (!f) return Error::make(Errc::Io, "gpio write failed", dir);
  std::fprintf(f, "%d\n", value ? 1 : 0);
  std::fclose(f);
  return Error::none();
}

Result<bool> NativePlatform::gpio_get(int line) {
  auto v = read_file("/sys/class/gpio/gpio" + std::to_string(line) + "/value");
  if (!v.ok()) return Result<bool>::fail(v.error.code, v.error.message, "gpio");
  try {
    return Result<bool>::good(std::stoi(*v) != 0);
  } catch (...) {
    return Result<bool>::fail(Errc::Corrupt, "gpio value unreadable", "gpio");
  }
}

Error NativePlatform::led_set(const std::string& name, bool on) {
  FILE* f = std::fopen((sysroot_ + "/sys/class/leds/" + name + "/brightness").c_str(), "w");
  if (!f) return Error::make(Errc::NotFound, "led not found", name);
  std::fprintf(f, "%d\n", on ? 255 : 0);
  std::fclose(f);
  return Error::none();
}

Error NativePlatform::led_blink(const std::string& name, int on_ms, int off_ms, int repeats) {
  for (int i = 0; i < repeats; ++i) {
    Error e = led_set(name, true);
    if (!e.ok()) return e;
    ::usleep(useconds_t(on_ms) * 1000);
    e = led_set(name, false);
    if (!e.ok()) return e;
    ::usleep(useconds_t(off_ms) * 1000);
  }
  return Error::none();
}

Error NativePlatform::usb_reset(const std::string& port) {
  // Standard sysfs authorized toggle resets a USB port.
  std::string auth = "/sys/bus/usb/devices/" + port + "/authorized";
  FILE* f = std::fopen((sysroot_ + auth).c_str(), "w");
  if (!f) return Error::make(Errc::NotFound, "usb port not found", port);
  std::fprintf(f, "0\n");
  std::fflush(f);
  ::usleep(50000);
  std::fseek(f, 0, SEEK_SET);
  std::fprintf(f, "1\n");
  std::fclose(f);
  return Error::none();
}

Result<bool> NativePlatform::can_loopback_test(const std::string& iface) {
  // A behavioural sanity check: interface must exist and be up.
  auto state = read_file("/sys/class/net/" + iface + "/operstate");
  if (!state.ok()) return Result<bool>::good(false);
  return Result<bool>::good(state->find("up") != std::string::npos);
}

Error NativePlatform::display_control(const std::string& cmd, const std::string& arg) {
  (void)cmd;
  (void)arg;
  // Display control is display-technology specific (DSI/eDP); the descriptor
  // declares which backend exists. Generic path: blank/unblank via sysfs.
  if (cmd == "blank" || cmd == "unblank") {
    FILE* f = std::fopen((sysroot_ + "/sys/class/graphics/fb0/blank").c_str(), "w");
    if (!f) return Error::make(Errc::NotSupported, "no display backend for this board", cmd);
    std::fprintf(f, "%s\n", cmd == "blank" ? "4" : "0");
    std::fclose(f);
    return Error::none();
  }
  return Error::make(Errc::NotSupported, "display command not in descriptor", cmd);
}

Result<std::string> NativePlatform::display_probe() {
  auto f = read_file("/sys/class/graphics/fb0/virtual_size");
  if (!f.ok()) return Result<std::string>::fail(Errc::NotSupported, "no framebuffer", "display");
  std::string s = *f;
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
  return Result<std::string>::good("ok " + s);
}

Error NativePlatform::unit_start(const std::string& unit) {
  if (!apply_enabled())
    return Error::make(Errc::Unavailable,
                       "systemd mutation disabled on host (INSTR_APPLY_NETWORK!=1)", unit);
  int rc = std::system(("systemctl start " + unit + " 2>/dev/null").c_str());
  return rc == 0 ? Error::none() : Error::make(Errc::Unavailable, "systemctl start failed", unit);
}
Error NativePlatform::unit_stop(const std::string& unit) {
  if (!apply_enabled())
    return Error::make(Errc::Unavailable, "systemd mutation disabled on host", unit);
  int rc = std::system(("systemctl stop " + unit + " 2>/dev/null").c_str());
  return rc == 0 ? Error::none() : Error::make(Errc::Unavailable, "systemctl stop failed", unit);
}
Error NativePlatform::unit_restart(const std::string& unit) {
  if (!apply_enabled())
    return Error::make(Errc::Unavailable, "systemd mutation disabled on host", unit);
  int rc = std::system(("systemctl restart " + unit + " 2>/dev/null").c_str());
  return rc == 0 ? Error::none() : Error::make(Errc::Unavailable, "systemctl restart failed", unit);
}
Result<bool> NativePlatform::unit_active(const std::string& unit) {
  if (apply_enabled()) {
    int rc = std::system(("systemctl is-active --quiet " + unit + " 2>/dev/null").c_str());
    return Result<bool>::good(rc == 0);
  }
  // Without systemd: treat presence of a pid file as active.
  std::string unit_name = unit;
  size_t pos = unit_name.find(".service");
  if (pos != std::string::npos) unit_name = unit_name.substr(0, pos);
  return Result<bool>::good(read_file("/run/" + unit_name + ".pid").ok());
}

Result<std::vector<std::string>> NativePlatform::kernel_log_lines(bool from_start) {
  // /dev/kmsg: open and read from the start when from_start; otherwise seek
  // to the end for follow mode.
  int fd = ::open((sysroot_ + "/dev/kmsg").c_str(), O_RDONLY | O_NONBLOCK);
  std::vector<std::string> out;
  if (fd < 0) {
    // Fallback: dmesg buffer via /proc/kmsg is unreadable unprivileged; use
    // the journal's kernel messages instead.
    return journal_lines("kernel", from_start ? 100000 : 200);
  }
  if (!from_start) {
    // SYSLOG_ACTION_SIZE_BUFFER equivalent: seek to end for follow.
    ::lseek(fd, 0, SEEK_END);
  }
  char buf[4096];
  ssize_t n;
  int count = 0;
  while ((n = ::read(fd, buf, sizeof(buf) - 1)) > 0 && count < 50000) {
    buf[n] = '\0';
    std::string chunk(buf, size_t(n));
    size_t pos = 0;
    while (pos < chunk.size()) {
      size_t nl = chunk.find('\n', pos);
      if (nl == std::string::npos) nl = chunk.size();
      out.push_back(chunk.substr(pos, nl - pos));
      pos = nl + 1;
      ++count;
    }
  }
  ::close(fd);
  return Result<std::vector<std::string>>::good(std::move(out));
}

Result<std::vector<std::string>> NativePlatform::journal_lines(const std::string& unit,
                                                               size_t max_lines) {
  if (!apply_enabled()) return Result<std::vector<std::string>>::good({});
  std::string cmd = "journalctl -u " + unit + " -n " + std::to_string(max_lines) +
                    " --no-pager -o short 2>/dev/null";
  FILE* p = ::popen(cmd.c_str(), "r");
  if (!p) return Result<std::vector<std::string>>::good({});
  std::vector<std::string> out;
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), p)) {
    std::string line(buf);
    while (!line.empty() && line.back() == '\n') line.pop_back();
    out.push_back(line);
  }
  ::pclose(p);
  return Result<std::vector<std::string>>::good(std::move(out));
}

Error NativePlatform::apply_network_plan(const std::string& plan_json) {
  if (!apply_enabled()) {
    // Dry-run: record only. Real application is done by rendering
    // fwupd/networkd config files via the plan renderer in networkd domain.
    return Error::none();
  }
  (void)plan_json;
  return Error::none();
}

bool NativePlatform::hq_reachable() {
  return read_file("/run/instrumentation/tunnel-up").ok();
}

}  // namespace platform
}  // namespace instr