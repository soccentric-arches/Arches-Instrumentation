// faults daemon facade: core + command table + spoke.
#pragma once
#include <memory>

#include "instr/common/command_table.h"
#include "instr/faults/domain.h"
#include "instr/transport/transport.h"
#include "faults.grpc.pb.h"

namespace instr {
namespace faults {
using namespace domain;
using namespace instr::transport;

class FaultsCore {
 public:
  FaultsCore() : processor_(std::vector<FaultRule>{}) {}

  Error load_rules(const std::string& yaml_text) {
    auto parsed = domain::load_rules(yaml_text);
    if (!parsed.ok()) return parsed.error;
    rules_text_ = yaml_text;
    processor_ = FaultProcessor(*parsed);
    return Error::none();
  }
  const std::string& rules_text() const { return rules_text_; }

  std::vector<FaultRecord> feed(const std::vector<std::string>& lines, uint64_t mono_ns,
                                bool backfill) {
    auto out = (*processor_).feed(lines, mono_ns, backfill);
    for (const auto& r : out) {
      if (sink_) sink_(r);
    }
    return out;
  }
  void set_sink(std::function<void(const FaultRecord&)> f) { sink_ = std::move(f); }
  const FaultProcessor::Stats& stats() const { return (*processor_).stats(); }
  const std::vector<FaultRule>& rules() const { return processor_ ? (*processor_).engine_rules() : empty_rules_; }

 private:
  std::string rules_text_;
  std::optional<FaultProcessor> processor_;
  std::vector<FaultRule> empty_rules_;
  std::function<void(const FaultRecord&)> sink_;
};

CommandTable build_faults_table(FaultsCore* core);

class FaultsSpoke {
 public:
  FaultsSpoke(FaultsCore* core, Logger* log, const CommandTable* table,
              const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_fault(const FaultRecord& r);
  void send_stats();

 private:
  FaultsCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<SpokeChannel<faults::FaultHub, faults::FaultSpokeFrame,
                               faults::FaultHubFrame>>
      channel_;
};

}  // namespace faults
}  // namespace instr