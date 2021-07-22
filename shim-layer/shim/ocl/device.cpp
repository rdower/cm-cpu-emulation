/*===================== begin_copyright_notice ==================================

 Copyright (c) 2021, Intel Corporation


 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
======================= end_copyright_notice ==================================*/

#include <cassert>
#include <unordered_map>

#if !defined(__GNUC__) || __clang__ || __GNUC__ > 7
# include <filesystem>
namespace fs = std::filesystem;
#else
# include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#include <dlfcn.h>

#include "device.h"
#include "platform.h"
#include "runtime.h"

namespace shim {
namespace cl {

static std::vector<std::string> IncludePath() {
  Dl_info info;
  if (!dladdr(reinterpret_cast<void*>(IncludePath), &info)) {
    throw std::runtime_error("Cannot get CM EMU headers");
  }

  std::vector<std::string> result;

  auto emudir = fs::path(info.dli_fname).parent_path().parent_path();

  auto directories = {
    emudir / "include" / "libcm",
    emudir / "include" / "shim",
  };

  for (auto inc: directories) {
    if (fs::is_directory(inc)) {
      result.emplace_back(inc);
    }
  }

  return result;
}

static std::vector<std::string> LibPath() {
  Dl_info info;
  if (!dladdr(reinterpret_cast<void*>(IncludePath), &info)) {
    throw std::runtime_error("Cannot get CM EMU headers");
  }

  std::vector<std::string> result;
  auto emudir = fs::path(info.dli_fname).parent_path();

  result.push_back(emudir);
  return result;
}

Device::Device(cl_icd_dispatch *dispatch) {
  this->dispatch = dispatch;

  CmDevice *dev = nullptr;
  unsigned version = 0;

  if (auto status = ::CreateCmDevice(dev, version);
      status != CM_SUCCESS || version < CM_1_0) {
    throw Error(::GetCmErrorString(status));
  }

  device = std::shared_ptr<CmDevice>(dev, [](CmDevice *dev) { ::DestroyCmDevice(dev); });
}

int Device::Id() const {
  int platform = PLATFORM_INTEL_UNKNOWN;
  size_t sz = sizeof(platform);
  if (auto status = device->GetCaps(CAP_GPU_PLATFORM, sz, &platform);
      status != CM_SUCCESS) {
    throw Error(::GetCmErrorString(status));
  }
  return platform;
}

std::string_view Device::Name() const {
  using namespace std::literals;
#define P(x) { PLATFORM_INTEL_ ## x, #x ## sv }
  static const std::unordered_map<int, std::string_view> platforms = {
    P(SKL),
    P(KBL),
    P(ICLLP),
    P(TGLLP),
    P(XEHP_SDV),
  };
#undef P

  auto it = platforms.find(Id());
  if (it == platforms.end()) {
    return "Unknown platform"sv;
  }

  return it->second;
}

std::string Device::CompilerCommand() const {
  using namespace std::literals;
#define P(platform, flags) { PLATFORM_INTEL_ ## platform, flags }
  static const std::unordered_map<int, std::string_view> platforms = {
    P(SKL, "-DCM_GEN9 -DHAS_LONG_LONG -DHAS_DOUBLE"sv),
    P(KBL, "-DCM_GEN9 -DHAS_LONG_LONG -DHAS_DOUBLE"sv),
    P(ICLLP, "-DCM_GEN11 -DHAS_LONG_LONG"sv),
    P(TGLLP, "-DCM_GEN12 -DHAS_LONG_LONG"sv),
    P(XEHP_SDV, "-DCM_GEN12_5 -DHAS_LONG_LONG -DHAS_DOUBLE"sv),
  };
#undef P

  std::string result = "g++ -c -g2 -fPIC -rdynamic -x c++ -std=c++17 -Wno-attributes -DCMRT_EMU";

  for (auto &inc: IncludePath()) {
    result += " -isystem "sv;
    result += inc;
  }

  auto it = platforms.find(Id());
  if (it != platforms.end()) {
    result += " "sv;
    result += it->second;
  }

  return result;
}

std::string Device::LinkerCommand() const {
  std::string result = "g++ -g2 -Wl,--no-as-needed -shared -rdynamic -lcm";

  for (auto &lib: LibPath()) {
    result += " -L";
    result += lib;
  }

  return result;
}

} // namespace cl
} // namespace shim

CL_API_ENTRY cl_int CL_API_CALL
SHIM_CALL(clGetDeviceIDs)(cl_platform_id   platform,
                          cl_device_type   device_type,
                          cl_uint          num_entries,
                          cl_device_id *   devices,
                          cl_uint *        num_devices) CL_API_SUFFIX__VERSION_1_0 {
  auto &rt = shim::cl::Runtime::Instance();

  if (platform && platform != &rt.platform) {
    return CL_INVALID_PLATFORM;
  }

  if ((devices && num_entries < 1) || (!devices && !num_devices)){
    return CL_INVALID_VALUE;
  }

  if (!(device_type & (CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CUSTOM | CL_DEVICE_TYPE_DEFAULT))) {
    return CL_DEVICE_NOT_FOUND;
  }

  if (devices) {
    *devices = &rt.device;
  }
  if (num_devices) {
    *num_devices = 1;
  }

  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
SHIM_CALL(clGetDeviceInfo)(cl_device_id    device,
                           cl_device_info  param_name,
                           size_t          param_value_size,
                           void *          param_value,
                           size_t *        param_value_size_ret) CL_API_SUFFIX__VERSION_1_0 {
  using namespace std::literals;
  using shim::cl::SetResult;

  auto &rt = shim::cl::Runtime::Instance();

  if (device != &rt.device) {
    return CL_INVALID_DEVICE;
  }

  switch (param_name) {
    case CL_DEVICE_TYPE:
      return SetResult<cl_device_type>(CL_DEVICE_TYPE_CUSTOM, param_value_size,
                                       param_value, param_value_size_ret);
    case CL_DEVICE_VENDOR_ID:
      return SetResult<cl_uint>(0x8086, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_MAX_COMPUTE_UNITS:
      return SetResult<cl_uint>(1, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS:
      return SetResult<cl_uint>(3, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_MAX_WORK_GROUP_SIZE:
      return SetResult<size_t>(256, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_MAX_WORK_ITEM_SIZES: {
      static const std::array<size_t, 3> v = {{256, 256, 1}};
      return SetResult(v, param_value_size,
                       param_value, param_value_size_ret);
    }
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF:
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR:
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT:
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_INT:
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG:
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT:
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE:
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF:
      return SetResult<cl_uint>(1, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_MAX_CLOCK_FREQUENCY:
      return SetResult<cl_uint>(1000, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_ADDRESS_BITS:
      return SetResult<cl_uint>(sizeof(void*) * std::numeric_limits<unsigned char>::digits,
                                param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_MAX_READ_IMAGE_ARGS:
    case CL_DEVICE_MAX_WRITE_IMAGE_ARGS:
    case CL_DEVICE_MAX_MEM_ALLOC_SIZE:
    case CL_DEVICE_IMAGE2D_MAX_WIDTH:
    case CL_DEVICE_IMAGE2D_MAX_HEIGHT:
    case CL_DEVICE_IMAGE3D_MAX_WIDTH:
    case CL_DEVICE_IMAGE3D_MAX_HEIGHT:
    case CL_DEVICE_IMAGE3D_MAX_DEPTH:
    case CL_DEVICE_IMAGE_SUPPORT:
    case CL_DEVICE_MAX_PARAMETER_SIZE:
    case CL_DEVICE_MAX_SAMPLERS:
    case CL_DEVICE_MEM_BASE_ADDR_ALIGN:
    case CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE:
    case CL_DEVICE_SINGLE_FP_CONFIG:
    case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE:
    case CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE:
    case CL_DEVICE_GLOBAL_MEM_CACHE_SIZE:
    case CL_DEVICE_GLOBAL_MEM_SIZE:
    case CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE:
    case CL_DEVICE_MAX_CONSTANT_ARGS:
    case CL_DEVICE_LOCAL_MEM_TYPE:
    case CL_DEVICE_LOCAL_MEM_SIZE:
    case CL_DEVICE_ERROR_CORRECTION_SUPPORT:
    case CL_DEVICE_PROFILING_TIMER_RESOLUTION:
      break;
    case CL_DEVICE_ENDIAN_LITTLE:
      return SetResult<cl_bool>(CL_TRUE, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_AVAILABLE:
      return SetResult<cl_bool>(CL_TRUE, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_COMPILER_AVAILABLE:
      return SetResult<cl_bool>(CL_TRUE, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_EXECUTION_CAPABILITIES:

    // case CL_DEVICE_QUEUE_ON_HOST_PROPERTIES:
    case CL_DEVICE_QUEUE_PROPERTIES:
      break;
    case CL_DEVICE_NAME:
      return SetResult(rt.device.Name(), param_value_size,
                       param_value, param_value_size_ret);
    case CL_DEVICE_VENDOR:
      return SHIM_CALL(clGetPlatformInfo)(&rt.platform, CL_PLATFORM_VENDOR,
                                          param_value_size, param_value,
                                          param_value_size_ret);
    case CL_DRIVER_VERSION:

      return SetResult("1.0"sv, param_value_size,
                       param_value, param_value_size_ret);
    case CL_DEVICE_PROFILE:
      return SHIM_CALL(clGetPlatformInfo)(&rt.platform, CL_PLATFORM_PROFILE,
                                          param_value_size, param_value,
                                          param_value_size_ret);
    case CL_DEVICE_VERSION:
      return SHIM_CALL(clGetPlatformInfo)(&rt.platform, CL_PLATFORM_VERSION,
                                          param_value_size, param_value,
                                          param_value_size_ret);
    case CL_DEVICE_EXTENSIONS:
      return SHIM_CALL(clGetPlatformInfo)(&rt.platform, CL_PLATFORM_EXTENSIONS,
                                          param_value_size, param_value,
                                          param_value_size_ret);
    case CL_DEVICE_PLATFORM:
      return SetResult<cl_platform_id>(&rt.platform, param_value_size,
                                       param_value, param_value_size_ret);
    case CL_DEVICE_DOUBLE_FP_CONFIG:
    case CL_DEVICE_HALF_FP_CONFIG:
    case CL_DEVICE_HOST_UNIFIED_MEMORY:
      break;
    case CL_DEVICE_OPENCL_C_VERSION: {
      auto s = "OpenCL C 3.0 C-for-Metal"sv;
      return SetResult(s, param_value_size, param_value, param_value_size_ret);
    }
    case CL_DEVICE_LINKER_AVAILABLE:
      return SetResult<cl_bool>(CL_TRUE, param_value_size,
                                param_value, param_value_size_ret);
    case CL_DEVICE_BUILT_IN_KERNELS:
    case CL_DEVICE_IMAGE_MAX_BUFFER_SIZE:
    case CL_DEVICE_IMAGE_MAX_ARRAY_SIZE:
      break;
    case CL_DEVICE_PARENT_DEVICE:
      return SetResult<cl_device_id>(nullptr, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_PARTITION_MAX_SUB_DEVICES:
      return SetResult<cl_uint>(0, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_PARTITION_PROPERTIES:
    case CL_DEVICE_PARTITION_AFFINITY_DOMAIN:
    case CL_DEVICE_PARTITION_TYPE:
    case CL_DEVICE_REFERENCE_COUNT:
    case CL_DEVICE_PREFERRED_INTEROP_USER_SYNC:
    case CL_DEVICE_PRINTF_BUFFER_SIZE:
#ifdef CL_VERSION_2_0
    case CL_DEVICE_IMAGE_PITCH_ALIGNMENT:
    case CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT:
    case CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS:
    case CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE:
    case CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES:
    case CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE:
    case CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE:
    case CL_DEVICE_MAX_ON_DEVICE_QUEUES:
    case CL_DEVICE_MAX_ON_DEVICE_EVENTS:
      break;
    case CL_DEVICE_SVM_CAPABILITIES:
      return SetResult<cl_device_svm_capabilities>(CL_DEVICE_SVM_COARSE_GRAIN_BUFFER,
                                                   param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE:
    case CL_DEVICE_MAX_PIPE_ARGS:
    case CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS:
    case CL_DEVICE_PIPE_MAX_PACKET_SIZE:
    case CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT:
    case CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT:
    case CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT:
#endif
#ifdef CL_VERSION_2_1
    case CL_DEVICE_IL_VERSION:
    case CL_DEVICE_MAX_NUM_SUB_GROUPS:
    case CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS:
#endif
#ifdef CL_VERSION_3_0
      break;
    case CL_DEVICE_NUMERIC_VERSION:
      return SHIM_CALL(clGetPlatformInfo)(&rt.platform, CL_PLATFORM_NUMERIC_VERSION,
                                          param_value_size, param_value,
                                          param_value_size_ret);
    case CL_DEVICE_EXTENSIONS_WITH_VERSION:
      return SHIM_CALL(clGetPlatformInfo)(&rt.platform, CL_PLATFORM_EXTENSIONS_WITH_VERSION,
                                          param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_ILS_WITH_VERSION:
    case CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION:
    case CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES:
    case CL_DEVICE_ATOMIC_FENCE_CAPABILITIES:
    case CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT:
    case CL_DEVICE_OPENCL_C_ALL_VERSIONS:
    case CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE:
    case CL_DEVICE_WORK_GROUP_COLLECTIVE_FUNCTIONS_SUPPORT:
    case CL_DEVICE_GENERIC_ADDRESS_SPACE_SUPPORT:
      /* 0x106A to 0x106E - Reserved for upcoming KHR extension */
    case CL_DEVICE_OPENCL_C_FEATURES:
    case CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES:
    case CL_DEVICE_PIPE_SUPPORT:
    case CL_DEVICE_LATEST_CONFORMANCE_VERSION_PASSED:
#endif
      break;
  }

  // std::cerr << std::hex << "clGetDeviceInfo: 0x" << param_name << std::endl;
  return CL_INVALID_VALUE;
}

CL_API_ENTRY cl_int CL_API_CALL
SHIM_CALL(clRetainDevice)(cl_device_id device) CL_API_SUFFIX__VERSION_1_2 {
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
SHIM_CALL(clReleaseDevice)(cl_device_id device) CL_API_SUFFIX__VERSION_1_2 {
  return CL_SUCCESS;
}

extern "C" {
SHIM_EXPORT(clGetDeviceIDs);
SHIM_EXPORT(clGetDeviceInfo);
SHIM_EXPORT(clRetainDevice);
SHIM_EXPORT(clReleaseDevice);
}
