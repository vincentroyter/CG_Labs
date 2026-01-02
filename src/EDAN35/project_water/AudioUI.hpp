#pragma once
#include <vector>
#include <string>
#include <functional>

#include "AudioBandSource.hpp"

struct AudioUIState
{
	// Engine state (owned in project_water.cpp)
	bool* enabled = nullptr;
	float* volume = nullptr;

	// For showing playback info and seeking
	float* durationSec = nullptr;
	float* cursorSec = nullptr;
	std::function<void(float)> seekSeconds;

	// Load/play controls
	std::function<void()> onLoadWav;
	std::function<void()> onPlay;
	std::function<void()> onPause;
	std::function<void()> onStop;

	std::string* loadedPath = nullptr;
	bool* isLoaded = nullptr;
	bool* isPlaying = nullptr;

	// Spectrum
	const std::vector<float>* spectrum = nullptr; // analyzer.spectrum()
	uint32_t* sampleRate = nullptr;               // engine.sampleRate()

	// Band sources
	std::vector<AudioBandSource>* bands = nullptr;
	int* selectedBand = nullptr;
	int* bandCounter = nullptr;

	// Global mapping params for stability
	float* k_stiff = nullptr;
	float* d_damp = nullptr;

	bool* showSpectrumWindow = nullptr;
	bool* freezeSpectrum = nullptr;
	float* spectrumSmooth = nullptr; // 0..1
	float* spectrumMaxHz = nullptr;  // default 20000


};

class AudioUI
{
public:
	void draw(AudioUIState& s);
	void drawSpectrumWindow(AudioUIState& s);

};
