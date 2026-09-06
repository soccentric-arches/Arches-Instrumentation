#include "instr/faults/daemon.h"

namespace instr {
namespace faults {

static JValue record_json(const FaultRecord& r) {
  JValue o = JValue::object();
  o.set("fault_id", JValue(double(r.fault_id)));
  o.set("id_text", JValue(alarm_id_string(r.fault_id)));
  o.set("rule", JValue(r.rule));
  o.set("severity", JValue(r.severity == common::SEV_CRITICAL ? "CRITICAL"
                          : r.severity == common::SEV_MAJOR   ? "MAJOR"
                          : r.severity == common::SEV_MINOR   ? "MINOR"
                          : r.severity == common::SEV_WARNING ? "WARNING"
                                                              : "INFO"));
  o.set("raw_line", JValue(r.raw_line));
  o.set("occurrences", JValue(double(r.occurrences)));
  o.set("device_key", JValue(r.device_key));
  o.set("x733_type", JValue(r.x733_type));
  o.set("backfill", JValue(r.backfill));
  return o;
}

CommandTable build_faults_table(FaultsCore* core) {
  CommandTable t;
  t.add({"stats", "scan statistics (lines, faults, duplicates, suppressed)", {},
         [core](const std::string&) {
           const auto& s = core->stats();
           JValue o = JValue::object();
           o.set("lines_scanned", JValue(double(s.lines_scanned)));
           o.set("faults_raised", JValue(double(s.faults_raised)));
           o.set("duplicates_merged", JValue(double(s.duplicates_merged)));
           o.set("backfill_lines", JValue(double(s.backfill_lines)));
           return CommandResult::good(o.dump());
         }});
  t.add({"rules-list", "list loaded fault rules", {},
         [core](const std::string&) {
           JValue arr = JValue::array();
           for (const auto& r : core->rules()) {
             JValue o = JValue::object();
             o.set("name", JValue(r.name));
             o.set("pattern", JValue(r.pattern));
             o.set("id_text", JValue(alarm_id_string(r.fault_id)));
             o.set("severity", JValue(r.severity == common::SEV_CRITICAL ? "CRITICAL"
                                     : r.severity == common::SEV_MAJOR   ? "MAJOR"
                                     : r.severity == common::SEV_MINOR   ? "MINOR"
                                     : r.severity == common::SEV_WARNING ? "WARNING"
                                                                         : "INFO"));
             o.set("x733_type", JValue(r.x733_type));
             arr.push(o);
           }
           return CommandResult::good(arr.dump());
         }});
  t.add({"rules-reload", "reload the rule set from text (rules are data)",
         {{"yaml", true, "full fault-rules.yaml document"}},
         [core](const std::string& args) {
           auto a = args_object(args);
           if (!a.ok()) return CommandResult::from_error(a.error);
           Error e = core->load_rules((*a).get_string("yaml"));
           return e.ok() ? CommandResult::good(std::string("{\"reloaded\":true}"))
                         : CommandResult::from_error(e);
         }});
  t.add({"test-line", "test a kernel line against the rule set",
         {{"line", true, "kernel log line"}},
         [core](const std::string& args) {
           auto a = args_object(args);
           if (!a.ok()) return CommandResult::from_error(a.error);
           auto recs = core->feed({(*a).get_string("line")}, 1'000'000'000ull, false);
           JValue arr = JValue::array();
           for (const auto& r : recs) arr.push(record_json(r));
           return CommandResult::good(arr.dump());
         }});
  return t;
}

FaultsSpoke::FaultsSpoke(FaultsCore* core, Logger* log, const CommandTable* table,
                         const std::string& hub_target, const std::string& build_version)
    : core_(core), log_(log), table_(table) {
  using ChannelT =
      SpokeChannel<faults::FaultHub, faults::FaultSpokeFrame, faults::FaultHubFrame>;
  ChannelT::Options o;
  o.target = hub_target;
  o.daemon = "faults";
  o.build_version = build_version;
  o.capabilities = {"kernel-log", "backfill", "dedup", "rate-limit"};
  channel_ = std::make_unique<ChannelT>(std::move(o), *log);
  channel_->set_hub_handler([this](const faults::FaultHubFrame& hf) {
    if (hf.has_command()) {
      common::Ack ack = dispatch_to_ack(table_, hf.command());
      channel_->send_ack(ack);
    }
  });
  core_->set_sink([this](const FaultRecord& r) { send_fault(r); });
}

void FaultsSpoke::start() { channel_->start(); }
void FaultsSpoke::stop() { channel_->stop(); }

void FaultsSpoke::send_fault(const FaultRecord& r) {
  channel_->enqueue([&r](faults::FaultSpokeFrame& f) {
    auto* ev = f.mutable_event();
    ev->set_fault_id(r.fault_id);
    ev->mutable_ts()->set_mono_ns(r.mono_ns);
    ev->set_rule(r.rule);
    ev->set_severity(r.severity);
    ev->set_raw_line(r.raw_line);
    ev->set_occurrences(r.occurrences);
    ev->set_device_key(r.device_key);
    ev->set_x733_type(r.x733_type);
    ev->set_probable_cause(r.probable_cause);
    ev->set_backfill(r.backfill);
  });
}

void FaultsSpoke::send_stats() {
  const auto& s = core_->stats();
  JValue o = JValue::object();
  o.set("lines_scanned", JValue(double(s.lines_scanned)));
  o.set("faults_raised", JValue(double(s.faults_raised)));
  o.set("duplicates_merged", JValue(double(s.duplicates_merged)));
  o.set("suppressed_events", JValue(double(s.suppressed_events)));
  o.set("backfill_lines", JValue(double(s.backfill_lines)));
  channel_->send_status_json(o.dump());
}

}  // namespace faults
}  // namespace instr