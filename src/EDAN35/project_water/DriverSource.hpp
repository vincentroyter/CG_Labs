#pragma once

#include <string>
#include <glm/glm.hpp>

enum class DriverType { Point, Line };

struct DriverSource
{
	DriverType type = DriverType::Point;

	bool enabled = true;
	bool oscOn = true;
	bool spinOn = false;

	// Oscillation
	float freqHz = 3.0f;
	float oscPhaseRad = 0.0f;

	// Target height (static OR oscillating)
	float amp = -0.20f;

	// Shape
	float width = 5.0f;
	glm::vec2 pos01 = glm::vec2(0.5f, 0.5f);

	// Spin / orbit
	float spinHz = 0.25f;
	float spinPhaseRad = 0.0f;
	float orbitRadius01 = 0.03f;

	// Line-only
	float angleRad = 0.0f;
	float length01 = 1.0f;

	std::string name;
};
