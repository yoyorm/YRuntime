#pragma once

// OutOfDate 是 resize / 最小化的正常分支，不能靠异常做控制流（D14）。
// 该宏必须对所有 TU 一致，所以只允许通过本头包含 vulkan 头。
#ifndef VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
#endif

#include <vulkan/vulkan_raii.hpp>