#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using namespace std::literals;
namespace logger = SKSE::log;
