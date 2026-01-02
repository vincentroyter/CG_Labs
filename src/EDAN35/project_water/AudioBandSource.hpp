#pragma once
#include <string>
#include <glm/vec2.hpp>

enum class AudioBandType
{
	Point = 0,
	Line = 1
};

// How this band drives the water surface.
//  - Envelope: slow/continuous target-height forcing (good for "breathing")
//  - Impulse:  onset/transient impulses (good for "it dances")
//  - Hybrid:   both
enum class AudioDriveMode
{
	Envelope = 0,
	Impulse = 1,
	Hybrid = 2
};

struct AudioBandSource
{
	bool enabled = true;
	std::string name = "Band";

	AudioBandType  type = AudioBandType::Point;
	AudioDriveMode mode = AudioDriveMode::Hybrid;

	// Frequency band in Hz
	float fLowHz = 0.0f;
	float fHighHz = 200.0f;

	// Where on the surface (0..1)
	glm::vec2 pos01 = glm::vec2(0.5f, 0.5f);

	// Point radius in cells OR Line width in cells
	float radiusCells = 12.0f;

	// Signed strength (push/pull). Allow negative.
	float gain = 10.0f;

	// Envelope gate
	float threshold = 0.0f;

	// Envelope smoothing (1/s)
	float attack = 15.0f;
	float release = 10.0f;

	float onsetHPTimeSec = 0.35f;  // 0.2..0.8, larger for bass
	float energySlow = 0.0f;   // state


	// --- Auto gain control (AGC) ---
	// Makes bands behave consistently across different songs/levels.
	bool  agcEnabled = true;
	float agcTimeSec = 2.0f;   // 0.5..5 sec is typical
	float agcFloor = 1e-6f;  // avoids division by tiny values
	float agcRunning = 1e-4f;  // running mean power

	// --- Onset / transient impulses ("it dances") ---
	float impulseGain = 20.0f; // signed: +push, -pull
	float onsetThreshold = 0.15f; // threshold on flux (after AGC)
	float impulseCooldownSec = 0.08f; // min time between triggers
	float fluxSmoothRate = 35.0f; // (1/s) smoothing on flux
	float cooldownTimer = 0.0f;  // state
	float prevEnergyNorm = 0.0f;  // state
	float fluxRaw = 0.0f;  // debug
	float fluxSmoothed = 0.0f;  // debug

	// Debug/state (kept here so UI can show them)
	float energyRaw = 0.0f;
	float energyNorm = 0.0f; // after AGC
	float energySmoothed = 0.0f; // envelope

	// Line-only
	float length01 = 0.5f; // 0..1
	float angleRad = 0.0f; // radians

	// Envelope path state
	float prevTargetU = 0.0f;
};
