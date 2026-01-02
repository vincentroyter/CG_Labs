#pragma once
#include <vector>
#include "WaterRenderer.hpp"     // WaterVisualParams
#include "DriverSource.hpp"
#include "AudioBandSource.hpp"

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
		bool showDriverWindow = false;
		bool showAudioWindow = false;
		bool showSpectrumWindow = false;
	} win;

	struct Audio {
		bool enabled = true;
		float volume = 1.0f;

		float spectrumSmooth = 0.5f;
		float spectrumMaxHz = 20000.0f;
		bool  freezeSpectrum = false;

		float k_stiff = 120.0f;
		float d_damp = 10.0f;

		std::vector<AudioBandSource> bands;
		int selectedBand = -1;
		int bandCounter = 0;
	} audio;

	// Persisted driver sources
	std::vector<DriverSource> drivers;
	int selectedDriver = -1;
	int driverCounter = 0;
};
