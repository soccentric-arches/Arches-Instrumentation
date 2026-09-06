// ============================================================================
//  Arches Instrumentation
//
//  Copyright (C) 2026 Soccentric. All rights reserved.
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  This file is part of a proprietary product of Soccentric.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file lib/ctrl/include/instr/ctrl/ctrl.h
 * @brief raw-UDS control server/client framework (§3.2)
 *
 * Each daemon exposes <daemon>-ctrl.sock, mode 0600, root-only. The ctrl
 * server dispatches from the same command table the gRPC surface uses.
 * Streaming ops (live tail) push frames until the client disconnects or the
 * op's timeout elapses.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

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

namespace instr
{
namespace ctrl
{

class StreamSink
{
  public:
    explicit StreamSink(int fd) : fd_(fd)
    {
    }
    bool send(const std::string& json_line);
    bool closed() const
    {
        return closed_;
    }

  private:
    int fd_;
    std::atomic<bool> closed_{false};
};

class CtrlServer
{
  public:
    CtrlServer(const CommandTable* table, std::string socket_path);
    ~CtrlServer();

    // Binds and listens; returns false with a diagnosable error on failure.
    Error start();
    void stop();
    bool running() const
    {
        return running_;
    }

    // Stream handlers: op → handler(args_json, sink). Registered separately
    // from the table (payload ops stay table-driven).
    void set_stream_handler(const std::string& op, std::function<void(const std::string& args, StreamSink&)> h)
    {
        streams_[op] = std::move(h);
    }

    // How many connection threads are currently tracked. Exposed so the
    // reaping in serve_accept() is testable: this grew without bound for the
    // life of the daemon before the done-flag reap replaced a joinable()
    // check that could never fire.
    size_t tracked_connections() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return conn_threads_.size();
    }

  private:
    void serve_accept();
    void serve_conn(int fd);

    const CommandTable* table_;
    std::string path_;
    // Atomic: the accept thread reads this while stop() closes and clears it
    // from another thread. As a plain int that is a data race (TSan flags it),
    // and on a weakly-ordered target — half this fleet is aarch64 — the accept
    // loop can miss the close and spin on a stale descriptor.
    std::atomic<int> listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    // Each connection thread flags itself done on exit; serve_accept() joins
    // the flagged ones before spawning more. A plain vector<thread> pruned by
    // joinable() never shrinks — a thread stays joinable after its function
    // returns — so that leaked one handle and one unjoined OS thread per
    // connection, for the life of the daemon.
    struct Conn
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::vector<Conn> conn_threads_;
    std::map<std::string, std::function<void(const std::string&, StreamSink&)>> streams_;
    mutable std::mutex mu_;
};

class CtrlClient
{
  public:
    explicit CtrlClient(std::string socket_path);
    ~CtrlClient();

    Error connect();
    void close();

    // Single request/response.
    CommandResult call(const std::string& op, const std::string& json_args);
    // Streaming: callback per streamed frame until end or error.
    Error call_stream(const std::string& op, const std::string& json_args, const std::function<void(const std::string& json)>& on_frame);

  private:
    bool send_all(const std::string& data);
    bool recv_frame(std::string* out);

    std::string path_;
    int fd_ = -1;
    // One decoder per connection: a single read() often carries several frames
    // (a stream burst), and a fresh decoder per call would discard the tail.
    frames::FrameDecoder dec_;
};

// ---- bulk data transfer (REQ §7.7) ----
// "Bulk transfer by handle — the control API carries a handle and metadata;
// the payload never traverses istate's control path."
//
// A bundle can be tens of megabytes. Base64ing it into a JSON control
// response (as this used to do) inflates it by a third, buffers all of it in
// memory at both ends, and routes it through the path that carries alarms
// and liveness — which is the traffic that must not be held up behind a
// support archive.
//
// So the payload gets its own socket: the control response names a handle,
// the caller connects to the bulk socket, sends the handle, and the server
// streams raw bytes until close. Nothing is framed and nothing is encoded.
class BulkServer
{
  public:
    // resolve: handle → absolute file path, or an error for an unknown handle.
    using Resolver = std::function<Result<std::string>(const std::string& handle)>;

    BulkServer(std::string socket_path, Resolver resolve) : path_(std::move(socket_path)), resolve_(std::move(resolve))
    {
    }
    ~BulkServer();

    Error start();
    void stop();
    const std::string& path() const
    {
        return path_;
    }
    uint64_t served() const
    {
        return served_.load();
    }

  private:
    void serve_accept();
    void serve_one(int fd);

    std::string path_;
    Resolver resolve_;
    // Atomic: the accept thread reads this while stop() closes and clears it
    // from another thread. As a plain int that is a data race (TSan flags it),
    // and on a weakly-ordered target — half this fleet is aarch64 — the accept
    // loop can miss the close and spin on a stale descriptor.
    std::atomic<int> listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> served_{0};
    std::thread accept_thread_;
};

// Reads a payload by handle from a bulk socket. `sink` is called with each
// chunk as it arrives, so a caller can stream to disk or onward to HQ
// without holding the whole bundle in memory.
Result<uint64_t> bulk_fetch(const std::string& socket_path, const std::string& handle, const std::function<bool(const char* data, size_t len)>& sink);

// Shared _ctrl main: --help, --help <op>, --json, --socket PATH, completion.
// Returns process exit code.
int tool_main(int argc, char** argv, const CommandTable& table, const std::string& prog_name, const std::string& default_socket);

} // namespace ctrl
} // namespace instr