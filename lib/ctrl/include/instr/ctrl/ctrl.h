// Arches Instrumentation — raw-UDS control server/client framework (§3.2).
// Each daemon exposes <daemon>-ctrl.sock, mode 0600, root-only. The ctrl
// server dispatches from the same command table the gRPC surface uses.
// Streaming ops (live tail) push frames until the client disconnects or the
// op's timeout elapses.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "instr/common/command_table.h"
#include "instr/common/frames.h"

namespace instr {
namespace ctrl {

class StreamSink {
 public:
  explicit StreamSink(int fd) : fd_(fd) {}
  bool send(const std::string& json_line);
  bool closed() const { return closed_; }

 private:
  int fd_;
  std::atomic<bool> closed_{false};
};

class CtrlServer {
 public:
  CtrlServer(const CommandTable* table, std::string socket_path);
  ~CtrlServer();

  // Binds and listens; returns false with a diagnosable error on failure.
  Error start();
  void stop();
  bool running() const { return running_; }

  // Stream handlers: op → handler(args_json, sink). Registered separately
  // from the table (payload ops stay table-driven).
  void set_stream_handler(const std::string& op,
                          std::function<void(const std::string& args, StreamSink&)> h) {
    streams_[op] = std::move(h);
  }

 private:
  void serve_accept();
  void serve_conn(int fd);

  const CommandTable* table_;
  std::string path_;
  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  std::vector<std::thread> conn_threads_;
  std::map<std::string, std::function<void(const std::string&, StreamSink&)>> streams_;
  std::mutex mu_;
};

class CtrlClient {
 public:
  explicit CtrlClient(std::string socket_path);
  ~CtrlClient();

  Error connect();
  void close();

  // Single request/response.
  CommandResult call(const std::string& op, const std::string& json_args);
  // Streaming: callback per streamed frame until end or error.
  Error call_stream(const std::string& op, const std::string& json_args,
                    const std::function<void(const std::string& json)>& on_frame);

 private:
  bool send_all(const std::string& data);
  bool recv_frame(std::string* out);

  std::string path_;
  int fd_ = -1;
};

// Shared _ctrl main: --help, --help <op>, --json, --socket PATH, completion.
// Returns process exit code.
int tool_main(int argc, char** argv, const CommandTable& table, const std::string& prog_name,
              const std::string& default_socket);

}  // namespace ctrl
}  // namespace instr