#pragma once

#include <vector>
#include <functional>

#include "DriverSource.hpp"

class WaterHeightfield;
class WaterMesh;
class WaterRenderer;

struct WaterUIState
{
	// Simulation
	float* waveSpeed = nullptr;
	float* velDamp = nullptr;
	float* maxSlope = nullptr;

	// Splash
	float* clickMag = nullptr;
	int* clickRadius = nullptr;

	// Visualization
	bool* nearestHeight = nullptr;
	bool* showNodes = nullptr;
	float* nodeEps = nullptr;
	float* nodeStrength = nullptr;
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

	// Slope (first derivative)
	bool* slopeEnabled = nullptr;
	float* slopeScale = nullptr;
	float* slopeThreshold = nullptr;
	float* slopeStrength = nullptr;
	float* slopeColor = nullptr; // points to float[3]

	// Curvature (second derivative)
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
};

class WaterUI
{
public:
	void draw(
		WaterUIState& state,
		int currentNsim,
		int currentNr,
		float fps
	);
};
