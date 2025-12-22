#pragma once

#include <string>

#include "sandook/test/utils/test_utils.h"

bool FillBlockDevice(const std::string& dev_path);
bool BenchBlockDeviceRead(const std::string& dev_path, BenchResults* results);
bool BenchBlockDeviceWrite(const std::string& dev_path, BenchResults* results);
