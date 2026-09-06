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
 * @file lib/common/include/instr/common/spool.h
 * @brief bounded, disk-backed spool with explicit drop
 *
 * policy (REQ §4, §7.1, §11). Nothing northbound depends on the tunnel being
 * available at the moment it is produced.
 * 
 * Drop policy when the spool is full: oldest telemetry first, then log refs,
 * then timeline events; alarms and faults are NEVER dropped. If only
 * protected records remain and the spool is still full, the append is
 * refused with BudgetExceeded so the caller can raise the alarm.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/filesystem.h"

namespace instr
{

enum class SpoolPriority : uint8_t
{
    Telemetry = 0, // dropped first
    LogRef = 1,
    Event = 2,
    Fault = 3, // protected
    Alarm = 4, // protected
};

struct SpoolConfig
{
    std::string dir;
    uint64_t max_bytes = 64 * 1024 * 1024;
    uint64_t segment_bytes = 4 * 1024 * 1024;
};

class DiskSpool
{
  public:
    DiskSpool(IFileSystem& fs, SpoolConfig cfg) : fs_(fs), cfg_(std::move(cfg))
    {
    }

    // Opens/creates the spool directory and rebuilds the in-memory index from
    // segment files (survives restart).
    Error open();

    struct Entry
    {
        uint64_t seq = 0;
        SpoolPriority priority = SpoolPriority::Telemetry;
        uint64_t mono_ns = 0;
        std::string payload;
    };

    // Append one record. Enforces the size budget with the drop policy above.
    // On success, *dropped (if non-null) reports how many older low-priority
    // records were evicted to make room — drops are reported, never silent.
    Error append(SpoolPriority prio, const std::string& payload, uint64_t mono_ns, int* dropped = nullptr);

    // Oldest-first snapshot of up to `max` records.
    std::vector<Entry> peek(size_t max) const;

    // Consume the oldest n records (after successful drain/northbound send).
    Error pop_front(size_t n);

    struct Stats
    {
        uint64_t count = 0;
        uint64_t bytes = 0;
        uint64_t dropped_telemetry = 0;
        uint64_t dropped_logref = 0;
        uint64_t dropped_event = 0;
        uint64_t refused_protected = 0; // appends refused because only protected records fit
    };
    const Stats& stats() const
    {
        return stats_;
    }

    size_t size() const
    {
        return index_.size();
    }
    uint64_t bytes() const
    {
        return bytes_;
    }

  private:
    struct Record
    {
        SpoolPriority prio;
        uint64_t mono_ns;
        uint32_t len;
    };
    Error append_segment();
    Error load_index();
    Error drop_one_oldest_droppable();
    Error erase_index_record(size_t victim);
    std::string segment_path(uint64_t seg_seq) const
    {
        return fs_.join(cfg_.dir, "spool-" + std::to_string(seg_seq) + ".seg");
    }

    IFileSystem& fs_;
    SpoolConfig cfg_;
    struct Seg
    {
        uint64_t seq;
        uint64_t bytes;
        uint64_t records;
    };
    std::deque<Seg> segments_;
    // In-memory index: record header + segment seq + offset.
    struct IdxRec
    {
        SpoolPriority prio;
        uint64_t mono_ns;
        uint32_t len;
        uint64_t seg;
        uint64_t off;
    };
    std::deque<IdxRec> index_;
    uint64_t next_seg_seq_ = 1;
    uint64_t next_seq_ = 1;
    uint64_t bytes_ = 0;
    uint64_t seg_offset_ = 0;
    Stats stats_;
};

} // namespace instr