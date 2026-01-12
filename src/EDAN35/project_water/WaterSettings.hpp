#pragma once
#include <vector>
#include <string>
#include <glm/vec2.hpp>

#include "WaterRenderer.hpp"

enum class AudioBandType
{
	Point = 0,
	Line = 1
};

struct AudioBandSource
{
	bool enabled = true;
	std::string name = "Band";

	AudioBandType  type = AudioBandType::Point;

	float fLowHz = 0.0f;
	float fHighHz = 200.0f;

	glm::vec2 pos01 = glm::vec2(0.5f, 0.5f);

	float radiusCells = 12.0f;

	float gain = 1.0f;
	float stiffness = 20;
	float damping = 20;

	float threshold = 0.15f;

	float attack = 15.0f;
	float release = 10.0f;

	bool  agcEnabled = true;
	float agcTimeSec = 2.0f;
	float agcFloor = 1e-6f;
	float agcRunning = 1e-4f;

	float energyRaw = 0.0f;
	float energyNorm = 0.0f;
	float energySmoothed = 0.0f;

	float length01 = 1.0f;
	float angleRad = 0.0f;

	float prevTargetU = 0.0f;
};


struct WaterSettings
{
	struct Resolution {
		int Nsim = 128;
		int Nr = 512;
	} res;

	struct Sim {
		float c = 1.5f;
		float velDamp = 2.7f;
		float maxSlope = 1.8f;
		float viscosity = 0.05f;
		bool  openBoundary = false;
		bool  lockWaterLevel = true;
	} sim;

	struct Mouse {
		float clickMag = 20.0f;
		int   clickRadius = 4;
	} mouse;

	struct Render {
		bool nearestHeight = false;
		WaterVisualParams vis{};
	} render;

	struct Windows {
		bool showAudioWindow = false;
		bool showSpectrumWindow = false;
	} win;

	struct Audio {
		bool enabled = true;
		float volume = 1.0f;

		float spectrumSmooth = 0.5f;
		float spectrumMaxHz = 20000.0f;
		bool  freezeSpectrum = false;

		std::vector<AudioBandSource> bands;
		int selectedBand = -1;
		int bandCounter = 0;
	} audio;

};
