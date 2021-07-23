/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/runtime_fallback/kernel/conversion/conversion.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include <utility>

#include "tfrt/dtype/dtype.h"
#include "tfrt/host_context/async_value_ref.h"
#include "tfrt/host_context/host_context.h"
#include "tfrt/support/error_util.h"
#include "tfrt/support/forward_decls.h"
#include "tfrt/support/ref_count.h"
#include "tfrt/tensor/conversion_registry.h"
#include "tfrt/tensor/conversion_utils.h"
#include "tfrt/tensor/dense_host_tensor.h"
#include "tfrt/tensor/host_tensor.h"
#include "tfrt/tensor/tensor.h"
#include "tfrt/tensor/tensor_shape.h"
#include "tensorflow/core/runtime_fallback/kernel/kernel_fallback_tensor.h"
#include "tensorflow/core/runtime_fallback/util/tensor_util.h"
#include "tensorflow/core/runtime_fallback/util/type_util.h"
#include "tfrt/host_context/device.h"  // from @tf_runtime

namespace tensorflow {
namespace tfd {
using tfrt::DenseHostTensor;

static tfrt::AsyncValueRef<tfrt::StringHostTensor>
ConvertKernelFallbackTensorToStringHostTensor(
    const KernelFallbackTensor& tensor, const tfrt::CpuDevice& src,
    const tfrt::CpuDevice& dst, const tfrt::ExecutionContext& exec_ctx) {
  auto* host = exec_ctx.host();
  assert(!IsUnsupported(tensor.metadata().dtype) && "Unsupported dtype");
  const auto* tf_tensor = tensor.GetTensor();
  assert(tf_tensor->dtype() == DT_STRING && "dtype is not DT_STRING");

  auto sht = tfrt::StringHostTensor::CreateUninitialized(
      tfd::GetTensorMetadata(*tf_tensor), host);
  const int64_t num_elems = tf_tensor->NumElements();
  const tensorflow::tstring* tstrings =
      reinterpret_cast<const tensorflow::tstring*>(tf_tensor->data());

  auto strings = sht->strings();
  for (int i = 0; i < num_elems; ++i) {
    strings[i] = tstrings[i];
  }

  return tfrt::MakeAvailableAsyncValueRef<tfrt::StringHostTensor>(
      host, std::move(*sht));
}

static KernelFallbackTensor ConvertStringHostTensorToKernelFallbackTensor(
    const tfrt::StringHostTensor& tensor, const tfrt::CpuDevice& src,
    const tfrt::CpuDevice& dst, const tfrt::ExecutionContext& exec_ctx) {
  assert(&src == &dst);

  auto tf_tensor = CopyShtToTfTensor(tensor);
  return KernelFallbackTensor(tensor.shape(), tensor.dtype(), tf_tensor);
}

tfrt::Expected<tfrt::DenseHostTensor>
ConvertKernelFallbackTensorToDenseHostTensor(
    const KernelFallbackTensor& tensor, const tfrt::CpuDevice& src,
    const tfrt::CpuDevice& dst, const tfrt::ExecutionContext& exec_ctx) {
  const auto* tf_tensor = tensor.GetTensor();
  void* data = tf_tensor->data();
  size_t size = tf_tensor->AllocatedBytes();
  tfrt::RCReference<tfrt::HostBuffer> host_buffer =
      tfrt::HostBuffer::CreateFromExternal(
          data, size, [tensor = std::move(*tf_tensor)](void*, size_t) {});
  // Assume HostBuffer::CreateFromExternal never fails.
  return tfrt::DenseHostTensor(tensor.metadata(), std::move(host_buffer));
}

static KernelFallbackTensor ConvertDenseHostTensorToKernelFallbackTensor(
    const tfrt::DenseHostTensor& tensor, const tfrt::CpuDevice& src,
    const tfrt::CpuDevice& dst, const tfrt::ExecutionContext& exec_ctx) {
  assert(&src == &dst);

  auto tf_tensor = MoveHostBufferToTfTensor(tensor.buffer().CopyRef(),
                                            tensor.dtype(), tensor.shape());
  return KernelFallbackTensor(tensor.shape(), tensor.dtype(), tf_tensor);
}

tfrt::Expected<KernelFallbackTensor> TransferKernelFallback(
    const KernelFallbackTensor& tensor, const tfrt::Device& src,
    const tfrt::Device& dst, const tfrt::ExecutionContext& exec_ctx) {
  if (!src.IsDeviceType(tfrt::CpuDevice::kDeviceType) ||
      !dst.IsDeviceType(tfrt::CpuDevice::kDeviceType)) {
    return tfrt::MakeStringError(
        "Support converting KernelFallback to another KernelFallback "
        "only if both src & dst devices are CPUs");
  }

  return KernelFallbackTensor::Create(*tensor.GetTensor());
}

void RegisterKernelFallbackTensorConversionFn(
    tfrt::TensorConversionFnRegistry* registry) {
  registry->AddTensorConversionFn(
      TFRT_CONVERSION(ConvertKernelFallbackTensorToDenseHostTensor));
  registry->AddTensorConversionFn(
      TFRT_CONVERSION(ConvertStringHostTensorToKernelFallbackTensor));
  registry->AddTensorConversionFn(
      TFRT_CONVERSION(ConvertKernelFallbackTensorToStringHostTensor));
  registry->AddTensorConversionFn(
      TFRT_CONVERSION(ConvertDenseHostTensorToKernelFallbackTensor));
  registry->AddTensorConversionFn(TFRT_CONVERSION(TransferKernelFallback));
}

}  // namespace tfd
}  // namespace tensorflow
