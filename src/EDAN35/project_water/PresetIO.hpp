#pragma once
#include "WaterSettings.hpp"

namespace PresetIO
{
	bool Save(const char* path, const WaterSettings& s);
	bool Load(const char* path, WaterSettings& s);
}
