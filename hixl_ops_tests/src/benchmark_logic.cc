/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and
 * conditions of CANN Open Software License Agreement Version 2.0 (the "License"). Please refer to
 * the License for details. You may not use this file except in compliance with the License. THIS
 * SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A
 * PARTICULAR PURPOSE. See LICENSE in the root of the software repository for the full text of the
 * License.
 */

#include "acltest/benchmark_logic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace acltest {
namespace {

std::string Trim(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { return {}; }
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1U);
}

uint64_t ParseMultiplier(char suffix)
{
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(suffix)))) {
        case 'B': return 1U;
        case 'K': return 1024ULL;
        case 'M': return 1024ULL * 1024ULL;
        case 'G': return 1024ULL * 1024ULL * 1024ULL;
        default: throw std::invalid_argument("unsupported byte-size suffix");
    }
}

}  // namespace

uint64_t ParseByteSize(const std::string &text)
{
    std::string value = Trim(text);
    if (value.empty()) { throw std::invalid_argument("byte size cannot be empty"); }
    uint64_t multiplier = 1U;
    if (value.size() >= 3U &&
        (value[value.size() - 2U] == 'i' || value[value.size() - 2U] == 'I') &&
        (value.back() == 'b' || value.back() == 'B')) {
        multiplier = ParseMultiplier(value[value.size() - 3U]);
        value.erase(value.size() - 3U);
    } else if (value.size() >= 2U && (value.back() == 'b' || value.back() == 'B') &&
               !std::isdigit(static_cast<unsigned char>(value[value.size() - 2U]))) {
        multiplier = ParseMultiplier(value[value.size() - 2U]);
        value.erase(value.size() - 2U);
    } else if (!std::isdigit(static_cast<unsigned char>(value.back()))) {
        multiplier = ParseMultiplier(value.back());
        value.pop_back();
    }
    if (value.empty() || value.front() == '-') {
        throw std::invalid_argument("invalid byte size: " + text);
    }
    size_t parsed = 0U;
    const uint64_t number = std::stoull(value, &parsed, 10);
    if (parsed != value.size() || number == 0U ||
        number > std::numeric_limits<uint64_t>::max() / multiplier) {
        throw std::invalid_argument("invalid or overflowing byte size: " + text);
    }
    return number * multiplier;
}

std::vector<uint64_t> ParseByteSizeList(const std::string &text)
{
    std::vector<uint64_t> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) { values.push_back(ParseByteSize(item)); }
    if (values.empty()) { throw std::invalid_argument("size list cannot be empty"); }
    return values;
}

std::vector<size_t> ParseCountList(const std::string &text)
{
    std::vector<size_t> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = Trim(item);
        size_t parsed = 0U;
        const uint64_t count = std::stoull(item, &parsed, 10);
        if (item.empty() || parsed != item.size() || count == 0U ||
            count > std::numeric_limits<size_t>::max()) {
            throw std::invalid_argument("invalid IO count: " + item);
        }
        values.push_back(static_cast<size_t>(count));
    }
    if (values.empty()) { throw std::invalid_argument("count list cannot be empty"); }
    return values;
}

std::vector<HixlTransferDesc> BuildDeviceDescriptors(TransferDirection direction,
                                                     const std::vector<AddressRange> &ranges)
{
    constexpr uint64_t kMaxSdmaTransferBytes = std::numeric_limits<uint32_t>::max();
    if (ranges.empty()) { throw std::invalid_argument("transfer range list cannot be empty"); }
    std::vector<HixlTransferDesc> descriptors;
    descriptors.reserve(ranges.size());
    for (const AddressRange &range : ranges) {
        if (range.local_addr == 0U || range.device_addr == 0U || range.length == 0U ||
            range.length > std::numeric_limits<uint64_t>::max() - range.local_addr ||
            range.length > std::numeric_limits<uint64_t>::max() - range.device_addr) {
            throw std::invalid_argument("invalid or overflowing transfer address range");
        }
        uint64_t local = range.local_addr;
        uint64_t device = range.device_addr;
        uint64_t remaining = range.length;
        while (remaining != 0U) {
            const uint64_t block = std::min(remaining, kMaxSdmaTransferBytes);
            HixlTransferDesc descriptor;
            descriptor.src_addr = direction == TransferDirection::kDeviceToHost ? device : local;
            descriptor.dst_addr = direction == TransferDirection::kDeviceToHost ? local : device;
            descriptor.length = block;
            descriptors.push_back(descriptor);
            local += block;
            device += block;
            remaining -= block;
        }
    }
    return descriptors;
}

SubmissionShape ComputeSubmissionShape(size_t descriptor_count)
{
    if (descriptor_count == 0U) {
        throw std::invalid_argument("descriptor count must be non-zero");
    }
    SubmissionShape shape;
    shape.descriptor_count = descriptor_count;
    shape.kernel_launch_count =
        (descriptor_count + static_cast<size_t>(kMaxDescriptorsPerLaunch) - 1U) /
        kMaxDescriptorsPerLaunch;
    size_t tasks_since_notify = 0U;
    size_t begin = 0U;
    while (begin < descriptor_count) {
        const size_t count =
            std::min(static_cast<size_t>(kMaxDescriptorsPerLaunch), descriptor_count - begin);
        const bool tail = begin + count == descriptor_count;
        if (tasks_since_notify + count >= kMaxInFlightRtsqTasks || tail) {
            ++shape.notify_count;
            tasks_since_notify = 0U;
        } else {
            tasks_since_notify += count;
        }
        begin += count;
    }
    return shape;
}

size_t ComputeAutoIterations(uint64_t bytes_per_iteration, uint64_t target_bytes)
{
    if (bytes_per_iteration == 0U || target_bytes == 0U) {
        throw std::invalid_argument("iteration byte counts must be non-zero");
    }
    const uint64_t quotient = target_bytes / bytes_per_iteration;
    const uint64_t rounded = quotient + ((target_bytes % bytes_per_iteration) != 0U ? 1U : 0U);
    return static_cast<size_t>(std::max<uint64_t>(5U, std::min<uint64_t>(500U, rounded)));
}

double Percentile(std::vector<double> values, double percentile)
{
    if (values.empty() || percentile < 0.0 || percentile > 1.0) {
        throw std::invalid_argument("invalid percentile input");
    }
    std::sort(values.begin(), values.end());
    const double position = percentile * static_cast<double>(values.size() - 1U);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    if (lower == upper) { return values[lower]; }
    return values[lower] +
           (values[upper] - values[lower]) * (position - static_cast<double>(lower));
}

std::string FormatByteSize(uint64_t bytes)
{
    const char *suffix = "B";
    double value = static_cast<double>(bytes);
    if (bytes % (1024ULL * 1024ULL) == 0U) {
        suffix = "MiB";
        value /= 1024.0 * 1024.0;
    } else if (bytes % 1024ULL == 0U) {
        suffix = "KiB";
        value /= 1024.0;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 2) << value << suffix;
    return out.str();
}

}  // namespace acltest
