#pragma once
#include <vector>
#include <string>
#include <functional>
#include <cstdint>

#include "WaterSettings.hpp"

struct AudioUIState
{
	WaterSettings* settings = nullptr;

	// runtime display / playback state
	float* durationSec = nullptr;
	float* cursorSec = nullptr;
	std::function<void(float)> seekSeconds;

	std::function<void()> onLoadWav;
	std::function<void()> onPlay;
	std::function<void()> onPause;
	std::function<void()> onStop;

	std::string* loadedPath = nullptr;
	bool* isLoaded = nullptr;
	bool* isPlaying = nullptr;

	const std::vector<float>* spectrum = nullptr;
	uint32_t* sampleRate = nullptr;
};

class AudioUI
{
public:
	void draw(AudioUIState& s);
	void drawSpectrumWindow(AudioUIState& s);
};
