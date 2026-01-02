#pragma once

#include <vector>
#include <functional>
#include "DriverSource.hpp"

struct WaterUIState
{
	// Simulation
	float* waveSpeed = nullptr;
	float* velDamp = nullptr;
	float* maxSlope = nullptr;
	float* viscosity = nullptr;
	bool* setLockWaterLevel = nullptr;


	// Mouse interaction
	float* clickMag = nullptr;
	int* clickRadius = nullptr;

	// Rendering basics
	bool* nearestHeight = nullptr;

	// Node lines
	bool* showNodes = nullptr;
	float* nodeEps = nullptr;
	float* nodeStrength = nullptr;

	// Boundary
	bool* openBoundary = nullptr;

	// Visual effects (shader)
	bool* useHeightColoring = nullptr;
	int* colorTheme = nullptr;

	// Specular
	bool* enableSpecular = nullptr;
	float* specularStrength = nullptr;
	float* specularPower = nullptr;

	// Derivative visualizers
	bool* velocityEnabled = nullptr;
	float* velocityScale = nullptr;
	float* velocityThreshold = nullptr;
	float* velocityStrength = nullptr;
	float* velocityColor = nullptr; // points to float[3]

	bool* slopeEnabled = nullptr;
	float* slopeScale = nullptr;
	float* slopeThreshold = nullptr;
	float* slopeStrength = nullptr;
	float* slopeColor = nullptr; // points to float[3]

	bool* curvatureEnabled = nullptr;
	float* curvatureScale = nullptr;
	float* curvatureThreshold = nullptr;
	float* curvatureStrength = nullptr;
	float* curvatureColor = nullptr; // points to float[3]

	// Resolution
	int* Nsim = nullptr;
	int* Nr = nullptr;
	std::function<void(int newNsim, int newNr)> applyResolution;

	// Drivers
	std::vector<DriverSource>* drivers = nullptr;
	int* selectedDriver = nullptr;
	int* driverCounter = nullptr;

	// Camera callbacks
	std::function<void()> setCameraTopDown;
	std::function<void()> setCameraDefault;
	std::function<void()> resetSurface;

	std::function<void()> savePreset;
	std::function<void()> loadPreset;

	// Window toggles
	bool* showDriverWindow = nullptr;
	bool* showAudioWindow = nullptr;
};

class WaterUI
{
public:
	void drawMain(WaterUIState& s, int currentNsim, int currentNr, float fps);
	void drawDriversWindow(WaterUIState& s);
};
