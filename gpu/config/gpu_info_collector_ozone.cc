// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/config/gpu_info_collector.h"

#include "base/logging.h"

namespace gpu {

CollectInfoResult CollectContextGraphicsInfo(GPUInfo* gpu_info) {
  return CollectBasicGraphicsInfo(gpu_info);
}

GpuIDResult CollectGpuID(uint32* vendor_id, uint32* device_id) {
  DCHECK(vendor_id && device_id);
  *vendor_id = 0;
  *device_id = 0;
  return kGpuIDNotSupported;
}

CollectInfoResult CollectBasicGraphicsInfo(GPUInfo* gpu_info) {
  gpu_info->can_lose_context = false;
  return kCollectInfoSuccess;
}

CollectInfoResult CollectDriverInfoGL(GPUInfo* gpu_info) {
  NOTIMPLEMENTED();
  return kCollectInfoNonFatalFailure;
}

void MergeGPUInfo(GPUInfo* basic_gpu_info,
                  const GPUInfo& context_gpu_info) {
  MergeGPUInfoGL(basic_gpu_info, context_gpu_info);
}

}  // namespace gpu_info_collector
