/*========================== begin_copyright_notice ============================

Copyright (C) 2020 Intel Corporation

SPDX-License-Identifier: MIT

============================= end_copyright_notice ===========================*/

#ifndef GUARD_common_type_device_cap_name_h
#define GUARD_common_type_device_cap_name_h
typedef enum _CM_DEVICE_CAP_NAME {
  CAP_KERNEL_COUNT_PER_TASK,
  CAP_KERNEL_BINARY_SIZE,
  CAP_SAMPLER_COUNT ,
  CAP_SAMPLER_COUNT_PER_KERNEL,
  CAP_BUFFER_COUNT ,
  CAP_SURFACE2D_COUNT,
  CAP_SURFACE3D_COUNT,
  CAP_SURFACE_COUNT_PER_KERNEL,
  CAP_ARG_COUNT_PER_KERNEL,
  CAP_ARG_SIZE_PER_KERNEL ,
  CAP_USER_DEFINED_THREAD_COUNT_PER_TASK,
  CAP_HW_THREAD_COUNT,
  CAP_SURFACE2D_FORMAT_COUNT,
  CAP_SURFACE2D_FORMATS,
  CAP_SURFACE3D_FORMAT_COUNT,
  CAP_SURFACE3D_FORMATS,
  CAP_GPU_PLATFORM,
  CAP_MIN_FREQUENCY,
  CAP_MAX_FREQUENCY,
  CAP_L3_CONFIG,
  CAP_GPU_CURRENT_FREQUENCY,
  CAP_USER_DEFINED_THREAD_COUNT_PER_TASK_NO_THREAD_ARG,
  CAP_USER_DEFINED_THREAD_COUNT_PER_MEDIA_WALKER,
  CAP_USER_DEFINED_THREAD_COUNT_PER_THREAD_GROUP,
  CAP_SURFACE2DUP_COUNT,
  CAP_PLATFORM_INFO,
  CAP_MAX_BUFFER_SIZE,
  CAP_MAX_SUBDEV_COUNT //for app to retrieve the total count of sub devices
} CM_DEVICE_CAP_NAME;

#endif // GUARD_common_type_device_cap_name_h
