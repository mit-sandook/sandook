#pragma once

#include "sandook/base/time.h"
#include "sandook/virtual_disk/virtual_disk.h"

constexpr auto kPayloadSizeBytes = 1 << sandook::kSectorShift;
constexpr auto kPayloadSizeSectors = kPayloadSizeBytes >> sandook::kSectorShift;

bool AllocateBlocksInVirtualDisk(sandook::VirtualDisk *vdisk,
                                 uint64_t payload_sectors);
bool FillVirtualDisk(sandook::VirtualDisk *vdisk, int payload_size_bytes);
bool RandReadsTask(sandook::VirtualDisk *vdisk, sandook::Duration task_duration,
                   int max_inflight_reqs, int payload_size_bytes);
bool RandWritesTask(sandook::VirtualDisk *vdisk,
                    sandook::Duration task_duration, int max_inflight_reqs,
                    int payload_size_bytes);
bool RandReadsWritesTask(sandook::VirtualDisk *vdisk,
                         sandook::Duration task_duration, int max_num_inflight,
                         int payload_size_bytes, double read_ratio);
