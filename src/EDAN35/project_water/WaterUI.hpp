#pragma once
#include <functional>
#include "WaterSettings.hpp"

struct WaterUIState
{
	WaterSettings* settings = nullptr;

	// Resolution apply callback (recreate sim/mesh/texture)
	std::function<void(int newNsim, int newNr)> applyResolution;

	// Camera callbacks
	std::function<void()> setCameraTopDown;
	std::function<void()> setCameraDefault;
	std::function<void()> resetSurface;

	// Presets
	std::function<void()> savePreset;
	std::function<void()> loadPreset;
};

class WaterUI
{
public:
	void drawMain(WaterUIState& s, int currentNsim, int currentNr, float fps);
	void drawDriversWindow(WaterUIState& s);
};
