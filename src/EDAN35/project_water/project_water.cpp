// project_water.cpp
#include "project_water.hpp"

#include "config.hpp"
#include "core/Bonobo.h"
#include "core/ShaderProgramManager.hpp"
#include "core/helpers.hpp"
#include "core/opengl.hpp"

#include "WaterHeightfield.hpp"
#include "WaterMesh.hpp"
#include "WaterRenderer.hpp"
#include "WaterUI.hpp"
#include "DriverSource.hpp"

#include "AudioEngine.hpp"
#include "AudioAnalyzer.hpp"
#include "AudioUI.hpp"
#include "AudioBandSource.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tinyfiledialogs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

static constexpr float PI = 3.14159265358979323846f;

// ============================================================
// Preset helpers (key=value)
// ============================================================
static inline std::string trim(std::string s)
{
	auto isws = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!s.empty() && isws((unsigned char)s.front())) s.erase(s.begin());
	while (!s.empty() && isws((unsigned char)s.back())) s.pop_back();
	return s;
}

static inline void writeKV(std::ostream& os, const char* key, const std::string& v) { os << key << "=" << v << "\n"; }
static inline void writeKV(std::ostream& os, const char* key, int v)               { os << key << "=" << v << "\n"; }
static inline void writeKV(std::ostream& os, const char* key, float v)             { os << key << "=" << v << "\n"; }
static inline void writeKV(std::ostream& os, const char* key, bool v)              { os << key << "=" << (v ? 1 : 0) << "\n"; }

static inline bool parseBool(const std::string& s, bool def = false)
{
	if (s.empty()) return def;
	return (std::stoi(s) != 0);
}
static inline int parseInt(const std::string& s, int def = 0)
{
	if (s.empty()) return def;
	return std::stoi(s);
}
static inline float parseFloat(const std::string& s, float def = 0.0f)
{
	if (s.empty()) return def;
	return std::stof(s);
}

static std::unordered_map<std::string, std::string> loadPresetMap(const char* path)
{
	std::unordered_map<std::string, std::string> m;
	std::ifstream f(path);
	if (!f) return m;

	std::string line;
	while (std::getline(f, line)) {
		line = trim(line);
		if (line.empty()) continue;
		if (line[0] == '#' || line[0] == ';') continue;

		auto eq = line.find('=');
		if (eq == std::string::npos) continue;

		std::string k = trim(line.substr(0, eq));
		std::string v = trim(line.substr(eq + 1));
		m[k] = v;
	}
	return m;
}

static inline std::string getStr(const std::unordered_map<std::string, std::string>& m, const char* k, const char* def = "")
{
	auto it = m.find(k);
	return (it == m.end()) ? std::string(def) : it->second;
}
static inline int getInt(const std::unordered_map<std::string, std::string>& m, const char* k, int def = 0)
{
	auto it = m.find(k);
	return (it == m.end()) ? def : parseInt(it->second, def);
}
static inline float getFloat(const std::unordered_map<std::string, std::string>& m, const char* k, float def = 0.0f)
{
	auto it = m.find(k);
	return (it == m.end()) ? def : parseFloat(it->second, def);
}
static inline bool getBool(const std::unordered_map<std::string, std::string>& m, const char* k, bool def = false)
{
	auto it = m.find(k);
	return (it == m.end()) ? def : parseBool(it->second, def);
}

// ============================================================
// ProjectWater
// ============================================================
edan35::ProjectWater::ProjectWater(WindowManager& windowManager)
	: mCamera(0.5f * glm::half_pi<float>(),
		static_cast<float>(config::resolution_x) / static_cast<float>(config::resolution_y),
		0.01f, 200.0f)
	, inputHandler()
	, mWindowManager(windowManager)
	, window(nullptr)
{
	WindowManager::WindowDatum window_datum{ inputHandler, mCamera,
											config::resolution_x, config::resolution_y,
											0, 0, 0, 0 };

	window = mWindowManager.CreateGLFWWindow("EDAN35: Project Water", window_datum, config::msaa_rate);
	if (window == nullptr) {
		throw std::runtime_error("Failed to create window!");
	}

	bonobo::init();
}

edan35::ProjectWater::~ProjectWater()
{
	bonobo::deinit();
}

// ============================================================
// Picking helpers
// ============================================================
static bool pickWaterGrid(
	double mouseX, double mouseY,
	int screenW, int screenH,
	glm::mat4 const& world_to_clip,
	glm::mat4 const& model_to_world,
	float waterSize,
	int N,
	int& outGX, int& outGY)
{
	glm::mat4 clip_to_world = glm::inverse(world_to_clip);

	// NDC coords [-1, 1]
	float x = (2.0f * float(mouseX) / float(screenW)) - 1.0f;
	float y = 1.0f - (2.0f * float(mouseY) / float(screenH));

	// Ray in world
	glm::vec4 pNear = clip_to_world * glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 pFar  = clip_to_world * glm::vec4(x, y,  1.0f, 1.0f);
	pNear /= pNear.w;
	pFar  /= pFar.w;

	glm::vec3 ro = glm::vec3(pNear);
	glm::vec3 rd = glm::normalize(glm::vec3(pFar - pNear));

	// Water plane in world
	float planeY = (model_to_world * glm::vec4(0, 0, 0, 1)).y;

	// Ray-plane intersection
	const float denom = rd.y;
	if (std::abs(denom) < 1e-6f) return false;

	float t = (planeY - ro.y) / denom;
	if (t < 0.0f) return false; // behind camera

	glm::vec3 hit = ro + t * rd;

	// Convert hit point to water local space
	glm::mat4 world_to_model = glm::inverse(model_to_world);
	glm::vec3 hitLocal = glm::vec3(world_to_model * glm::vec4(hit, 1.0f));

	// Water spans x,z in [-size/2, +size/2]
	float half = 0.5f * waterSize;
	float u = (hitLocal.x + half) / waterSize;
	float v = (hitLocal.z + half) / waterSize;

	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return false;

	outGX = int(u * float(N - 1) + 0.5f);
	outGY = int(v * float(N - 1) + 0.5f);
	outGX = std::clamp(outGX, 1, N - 2);
	outGY = std::clamp(outGY, 1, N - 2);

	return true;
}

// Given a center (cx,cy) and a direction (dirx,diry), return the maximum
// half-length (in cells) you can extend in BOTH directions before hitting bounds.
static float maxHalfLenToBoundsCells(float cx, float cy, float dirx, float diry, int N)
{
	const float xmin = 0.0f, xmax = float(N - 1);
	const float ymin = 0.0f, ymax = float(N - 1);

	auto safeDiv = [](float a, float b) {
		return (std::abs(b) > 1e-6f) ? (a / b) : std::numeric_limits<float>::infinity();
	};

	// forward direction
	float t1 = safeDiv(xmin - cx, dirx);
	float t2 = safeDiv(xmax - cx, dirx);
	float t3 = safeDiv(ymin - cy, diry);
	float t4 = safeDiv(ymax - cy, diry);

	float tPos = std::numeric_limits<float>::infinity();
	if (t1 > 0.0f) tPos = std::min(tPos, t1);
	if (t2 > 0.0f) tPos = std::min(tPos, t2);
	if (t3 > 0.0f) tPos = std::min(tPos, t3);
	if (t4 > 0.0f) tPos = std::min(tPos, t4);

	// backward direction
	float t1n = safeDiv(xmin - cx, -dirx);
	float t2n = safeDiv(xmax - cx, -dirx);
	float t3n = safeDiv(ymin - cy, -diry);
	float t4n = safeDiv(ymax - cy, -diry);

	float tNeg = std::numeric_limits<float>::infinity();
	if (t1n > 0.0f) tNeg = std::min(tNeg, t1n);
	if (t2n > 0.0f) tNeg = std::min(tNeg, t2n);
	if (t3n > 0.0f) tNeg = std::min(tNeg, t3n);
	if (t4n > 0.0f) tNeg = std::min(tNeg, t4n);

	return std::min(tPos, tNeg);
}

// ============================================================
// Main loop
// ============================================================
void edan35::ProjectWater::run()
{
	// Camera setup
	mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 2.0f, 4.0f));
	mCamera.mWorld.SetRotateX(-0.5f);
	mCamera.mMouseSensitivity = glm::vec2(0.003f);
	mCamera.mMovementSpeed = glm::vec3(3.0f);

	// --- Camera presets ---
	auto setCameraDefault = [&]() {
		mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 2.0f, 4.0f));
		mCamera.mWorld.SetRotateX(-0.5f);
	};

	auto setCameraTopDown = [&]() {
		mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 6.0f, 0.001f));
		mCamera.mWorld.SetRotateX(-glm::half_pi<float>());
	};

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glClearColor(0.02f, 0.02f, 0.03f, 1.0f);

	// ---- Water objects ----
	const float SIZE = 4.0f;

	int Nsim = 128;  // simulation resolution
	int Nr   = 512;  // render resolution

	WaterHeightfield sim(Nsim, SIZE);
	WaterMesh        mesh(Nr, SIZE);
	WaterRenderer    renderer;

	renderer.setMesh(mesh.vertexData(), mesh.indices());
	renderer.updateHeightTexture(sim.packedHV(), sim.N());

	auto applyResolution = [&](int newNsim, int newNr)
	{
		newNsim = std::clamp(newNsim, 16, 512);
		newNr   = std::clamp(newNr,   32, 2048);

		Nsim = newNsim;
		Nr   = newNr;

		sim  = WaterHeightfield(Nsim, SIZE);
		mesh = WaterMesh(Nr, SIZE);

		renderer.setMesh(mesh.vertexData(), mesh.indices());
		renderer.updateHeightTexture(sim.packedHV(), sim.N());
	};

	// ---- Shader initialization ----
	ShaderProgramManager program_manager;
	GLuint water_shader = 0u;
	program_manager.CreateAndRegisterProgram(
		"Water",
		{ { ShaderType::vertex,   "EDAN35/water.vert" },
		  { ShaderType::fragment, "EDAN35/water.frag" } },
		water_shader
	);
	if (water_shader == 0u) {
		LogError("Failed to compile Water shader.");
		return;
	}

	bool show_gui = true;
	bool show_logs = false;
	bool show_driver_window = false;
	bool show_audio_window = false;

	// ---- Audio bands ----
	std::vector<AudioBandSource> audioBands;
	int selectedAudioBand = -1;
	int audioBandCounter = 1;

	auto releaseAudioBands = [&](float dt)
		{
			// Release speed: tweak to taste (higher = faster fade)
			const float envReleasePerSec = 6.0f;   // envelope -> 0
			const float fluxReleasePerSec = 10.0f;  // flux -> 0
			const float agcReleasePerSec = 2.0f;   // running mean -> 0 (optional)

			float aEnv = 1.0f - std::exp(-envReleasePerSec * dt);
			float aFlux = 1.0f - std::exp(-fluxReleasePerSec * dt);
			float aAgc = 1.0f - std::exp(-agcReleasePerSec * dt);

			for (auto& b : audioBands) {
				// kill impulses immediately (no more "stop spam" excitations)
				b.cooldownTimer = 0.0f;
				b.fluxRaw = 0.0f;

				// smoothly decay internal state
				b.fluxSmoothed = b.fluxSmoothed + aFlux * (0.0f - b.fluxSmoothed);
				b.energyRaw = 0.0f;
				b.energyNorm = 0.0f;
				b.energySlow = b.energySlow + aEnv * (0.0f - b.energySlow);
				b.energySmoothed = b.energySmoothed + aEnv * (0.0f - b.energySmoothed);
				b.prevTargetU = b.prevTargetU + aEnv * (0.0f - b.prevTargetU);

				if (b.agcEnabled) {
					b.agcRunning = b.agcRunning + aAgc * (0.0f - b.agcRunning);
				}
			}
		};


	// ---- Global UI params ----
	float ui_c           = sim.waveSpeed();
	float ui_velDamp     = sim.velDamp();
	float ui_maxSlope    = sim.maxSlope();
	bool  ui_useOpenBoundary = sim.isOpenBoundary();
	float ui_viscosity   = sim.viscosity();
	bool  ui_setLockWaterLevel = true;

	// Mouse interaction UI
	float ui_clickMag = 20.0f;
	int   ui_clickRadius = 4;

	bool ui_nearestHeight = false;

	// Node visualisation
	bool  ui_showNodes = true;
	float ui_nodeEps = 0.01f;
	float ui_nodeStrength = 0.85f;

	// Height coloring effect
	bool ui_useHeightColoring = false;
	int  ui_colorTheme = 0; // 0=Ocean, 1=Thermal, 2=Psychedelic

	// Specular
	bool  ui_enableSpecular = true;
	float ui_specularStrength = 0.15f;
	float ui_specularPower = 64.0f;

	// Derivative visualizers
	bool  ui_velocityEnabled = false;
	float ui_velocityScale = 1.0f;
	float ui_velocityThreshold = 0.0f;
	float ui_velocityStrength = 1.0f;
	float ui_velocityColor[3] = { 0.6f, 0.95f, 1.0f };

	bool  ui_slopeEnabled = false;
	float ui_slopeScale = 1.0f;
	float ui_slopeThreshold = 0.5f;
	float ui_slopeStrength = 1.0f;
	float ui_slopeColor[3] = { 0.92f, 0.98f, 1.0f };

	bool  ui_curvatureEnabled = false;
	float ui_curvatureScale = 10.0f;
	float ui_curvatureThreshold = 0.2f;
	float ui_curvatureStrength = 1.0f;
	float ui_curvatureColor[3] = { 1.0f, 0.6f, 0.2f };

	// Audio system
	AudioAnalyzer audioAnalyzer;
	AudioEngine   audioEngine;
	audioEngine.setAnalyzer(&audioAnalyzer);

	AudioUI audioUi;
	AudioUIState audioState;

	// Audio UI state vars
	bool  ui_audioEnabled = true;
	float ui_audioVolume = 1.0f;

	// Audio driver physics (stable)
	float ui_audio_k = 150.0f;
	float ui_audio_d = 18.0f;

	// Playback state mirror (for UI)
	bool  ui_audioLoaded = false;
	bool  ui_audioPlaying = false;
	std::string ui_audioPath;
	float ui_audioDuration = 0.0f;
	float ui_audioCursor = 0.0f;
	uint32_t ui_audioSampleRate = 0;

	// Spectrum UI
	bool  ui_showSpectrumWindow = false;
	bool  ui_freezeSpectrum = false;
	float ui_spectrumSmooth = 0.65f;
	float ui_spectrumMaxHz = 20000.0f;

	// Visual params bundle
	WaterVisualParams vis;

	// Resolution UI
	int ui_Nsim = Nsim;
	int ui_Nr   = Nr;

	// Drivers
	std::vector<DriverSource> drivers;
	int selectedDriver = -1;
	int driverCounter = 1;

	// FPS tracking
	float  fps_display = 0.0f;
	double fps_accum_time = 0.0;
	int    fps_accum_frames = 0;

	auto last = std::chrono::high_resolution_clock::now();
	bool shader_reload_failed = false;

	float simTime = 0.0f;

	// ============================================================
	// Presets (Save/Load)
	// ============================================================
	auto savePreset = [&](const char* path)
	{
		std::ofstream os(path);
		if (!os) return;

		os << "# WaterSim preset\n";

		// Resolution
		writeKV(os, "res.Nsim", ui_Nsim);
		writeKV(os, "res.Nr", ui_Nr);

		// Global sim
		writeKV(os, "sim.c", ui_c);
		writeKV(os, "sim.velDamp", ui_velDamp);
		writeKV(os, "sim.maxSlope", ui_maxSlope);
		writeKV(os, "sim.viscosity", ui_viscosity);
		writeKV(os, "sim.openBoundary", ui_useOpenBoundary);
		writeKV(os, "sim.lockWaterLevel", ui_setLockWaterLevel);

		// Interaction
		writeKV(os, "mouse.clickMag", ui_clickMag);
		writeKV(os, "mouse.clickRadius", ui_clickRadius);

		// Visual
		writeKV(os, "vis.nearestHeight", ui_nearestHeight);
		writeKV(os, "vis.showNodes", ui_showNodes);
		writeKV(os, "vis.nodeEps", ui_nodeEps);
		writeKV(os, "vis.nodeStrength", ui_nodeStrength);

		writeKV(os, "vis.useHeightColoring", ui_useHeightColoring);
		writeKV(os, "vis.colorTheme", ui_colorTheme);

		writeKV(os, "vis.enableSpecular", ui_enableSpecular);
		writeKV(os, "vis.specularStrength", ui_specularStrength);
		writeKV(os, "vis.specularPower", ui_specularPower);

		writeKV(os, "vis.velocityEnabled", ui_velocityEnabled);
		writeKV(os, "vis.velocityScale", ui_velocityScale);
		writeKV(os, "vis.velocityThreshold", ui_velocityThreshold);
		writeKV(os, "vis.velocityStrength", ui_velocityStrength);
		writeKV(os, "vis.velocityColorR", ui_velocityColor[0]);
		writeKV(os, "vis.velocityColorG", ui_velocityColor[1]);
		writeKV(os, "vis.velocityColorB", ui_velocityColor[2]);

		writeKV(os, "vis.slopeEnabled", ui_slopeEnabled);
		writeKV(os, "vis.slopeScale", ui_slopeScale);
		writeKV(os, "vis.slopeThreshold", ui_slopeThreshold);
		writeKV(os, "vis.slopeStrength", ui_slopeStrength);
		writeKV(os, "vis.slopeColorR", ui_slopeColor[0]);
		writeKV(os, "vis.slopeColorG", ui_slopeColor[1]);
		writeKV(os, "vis.slopeColorB", ui_slopeColor[2]);

		writeKV(os, "vis.curvatureEnabled", ui_curvatureEnabled);
		writeKV(os, "vis.curvatureScale", ui_curvatureScale);
		writeKV(os, "vis.curvatureThreshold", ui_curvatureThreshold);
		writeKV(os, "vis.curvatureStrength", ui_curvatureStrength);
		writeKV(os, "vis.curvatureColorR", ui_curvatureColor[0]);
		writeKV(os, "vis.curvatureColorG", ui_curvatureColor[1]);
		writeKV(os, "vis.curvatureColorB", ui_curvatureColor[2]);

		// Windows
		writeKV(os, "ui.showDriverWindow", show_driver_window);
		writeKV(os, "ui.showAudioWindow", show_audio_window);

		// Audio global
		writeKV(os, "audio.enabled", ui_audioEnabled);
		writeKV(os, "audio.volume", ui_audioVolume);
		writeKV(os, "audio.k", ui_audio_k);
		writeKV(os, "audio.d", ui_audio_d);

		// Spectrum UI
		writeKV(os, "audio.showSpectrum", ui_showSpectrumWindow);
		writeKV(os, "audio.freezeSpectrum", ui_freezeSpectrum);
		writeKV(os, "audio.spectrumSmooth", ui_spectrumSmooth);
		writeKV(os, "audio.spectrumMaxHz", ui_spectrumMaxHz);

		// Drivers
		writeKV(os, "drivers.count", (int)drivers.size());
		for (int i = 0; i < (int)drivers.size(); ++i) {
			auto const& d = drivers[i];
			std::string p = "driver." + std::to_string(i) + ".";
			writeKV(os, (p + "name").c_str(), d.name);
			writeKV(os, (p + "enabled").c_str(), d.enabled);
			writeKV(os, (p + "type").c_str(), (int)d.type);
			writeKV(os, (p + "posx").c_str(), d.pos01.x);
			writeKV(os, (p + "posy").c_str(), d.pos01.y);
			writeKV(os, (p + "width").c_str(), d.width);
			writeKV(os, (p + "amp").c_str(), d.amp);

			writeKV(os, (p + "oscOn").c_str(), d.oscOn);
			writeKV(os, (p + "freqHz").c_str(), d.freqHz);
			writeKV(os, (p + "oscPhaseRad").c_str(), d.oscPhaseRad);

			writeKV(os, (p + "spinOn").c_str(), d.spinOn);
			writeKV(os, (p + "spinHz").c_str(), d.spinHz);
			writeKV(os, (p + "spinPhaseRad").c_str(), d.spinPhaseRad);
			writeKV(os, (p + "orbitRadius01").c_str(), d.orbitRadius01);

			writeKV(os, (p + "angleRad").c_str(), d.angleRad);
			writeKV(os, (p + "length01").c_str(), d.length01);
		}

		// Bands
		writeKV(os, "bands.count", (int)audioBands.size());
		for (int i = 0; i < (int)audioBands.size(); ++i) {
			auto const& b = audioBands[i];
			std::string p = "band." + std::to_string(i) + ".";
			writeKV(os, (p + "name").c_str(), b.name);
			writeKV(os, (p + "enabled").c_str(), b.enabled);
			writeKV(os, (p + "type").c_str(), (int)b.type);
			writeKV(os, (p + "mode").c_str(), (int)b.mode);

			writeKV(os, (p + "fLowHz").c_str(), b.fLowHz);
			writeKV(os, (p + "fHighHz").c_str(), b.fHighHz);

			writeKV(os, (p + "posx").c_str(), b.pos01.x);
			writeKV(os, (p + "posy").c_str(), b.pos01.y);

			writeKV(os, (p + "gain").c_str(), b.gain);
			writeKV(os, (p + "threshold").c_str(), b.threshold);
			writeKV(os, (p + "radiusCells").c_str(), b.radiusCells);
			writeKV(os, (p + "attack").c_str(), b.attack);
			writeKV(os, (p + "release").c_str(), b.release);

			writeKV(os, (p + "agcEnabled").c_str(), b.agcEnabled);
			writeKV(os, (p + "agcTimeSec").c_str(), b.agcTimeSec);

			writeKV(os, (p + "impulseGain").c_str(), b.impulseGain);
			writeKV(os, (p + "onsetThreshold").c_str(), b.onsetThreshold);
			writeKV(os, (p + "impulseCooldownSec").c_str(), b.impulseCooldownSec);
			writeKV(os, (p + "fluxSmoothRate").c_str(), b.fluxSmoothRate);
			writeKV(os, (p + "onsetHPTimeSec").c_str(), b.onsetHPTimeSec);

			writeKV(os, (p + "angleRad").c_str(), b.angleRad);
			writeKV(os, (p + "length01").c_str(), b.length01);
		}
	};

	auto loadPreset = [&](const char* path)
	{
		auto m = loadPresetMap(path);
		if (m.empty()) return;

		// Windows
		show_driver_window = getBool(m, "ui.showDriverWindow", show_driver_window);
		show_audio_window  = getBool(m, "ui.showAudioWindow", show_audio_window);

		// Resolution (apply if changed)
		int newNsim = getInt(m, "res.Nsim", ui_Nsim);
		int newNr   = getInt(m, "res.Nr", ui_Nr);
		newNsim = std::clamp(newNsim, 16, 512);
		newNr   = std::clamp(newNr,   32, 2048);

		if (newNsim != Nsim || newNr != Nr) {
			ui_Nsim = newNsim;
			ui_Nr   = newNr;
			applyResolution(ui_Nsim, ui_Nr);
		} else {
			ui_Nsim = Nsim;
			ui_Nr   = Nr;
		}

		// Global sim
		ui_c           = getFloat(m, "sim.c", ui_c);
		ui_velDamp     = getFloat(m, "sim.velDamp", ui_velDamp);
		ui_maxSlope    = getFloat(m, "sim.maxSlope", ui_maxSlope);
		ui_viscosity   = getFloat(m, "sim.viscosity", ui_viscosity);
		ui_useOpenBoundary = getBool(m, "sim.openBoundary", ui_useOpenBoundary);
		ui_setLockWaterLevel = getBool(m, "sim.lockWaterLevel", ui_setLockWaterLevel);

		// Interaction
		ui_clickMag    = getFloat(m, "mouse.clickMag", ui_clickMag);
		ui_clickRadius = getInt(m, "mouse.clickRadius", ui_clickRadius);

		// Visual
		ui_nearestHeight = getBool(m, "vis.nearestHeight", ui_nearestHeight);
		ui_showNodes     = getBool(m, "vis.showNodes", ui_showNodes);
		ui_nodeEps       = getFloat(m, "vis.nodeEps", ui_nodeEps);
		ui_nodeStrength  = getFloat(m, "vis.nodeStrength", ui_nodeStrength);

		ui_useHeightColoring = getBool(m, "vis.useHeightColoring", ui_useHeightColoring);
		ui_colorTheme        = getInt(m, "vis.colorTheme", ui_colorTheme);

		ui_enableSpecular    = getBool(m, "vis.enableSpecular", ui_enableSpecular);
		ui_specularStrength  = getFloat(m, "vis.specularStrength", ui_specularStrength);
		ui_specularPower     = getFloat(m, "vis.specularPower", ui_specularPower);

		ui_velocityEnabled   = getBool(m, "vis.velocityEnabled", ui_velocityEnabled);
		ui_velocityScale     = getFloat(m, "vis.velocityScale", ui_velocityScale);
		ui_velocityThreshold = getFloat(m, "vis.velocityThreshold", ui_velocityThreshold);
		ui_velocityStrength  = getFloat(m, "vis.velocityStrength", ui_velocityStrength);
		ui_velocityColor[0]  = getFloat(m, "vis.velocityColorR", ui_velocityColor[0]);
		ui_velocityColor[1]  = getFloat(m, "vis.velocityColorG", ui_velocityColor[1]);
		ui_velocityColor[2]  = getFloat(m, "vis.velocityColorB", ui_velocityColor[2]);

		ui_slopeEnabled      = getBool(m, "vis.slopeEnabled", ui_slopeEnabled);
		ui_slopeScale        = getFloat(m, "vis.slopeScale", ui_slopeScale);
		ui_slopeThreshold    = getFloat(m, "vis.slopeThreshold", ui_slopeThreshold);
		ui_slopeStrength     = getFloat(m, "vis.slopeStrength", ui_slopeStrength);
		ui_slopeColor[0]     = getFloat(m, "vis.slopeColorR", ui_slopeColor[0]);
		ui_slopeColor[1]     = getFloat(m, "vis.slopeColorG", ui_slopeColor[1]);
		ui_slopeColor[2]     = getFloat(m, "vis.slopeColorB", ui_slopeColor[2]);

		ui_curvatureEnabled  = getBool(m, "vis.curvatureEnabled", ui_curvatureEnabled);
		ui_curvatureScale    = getFloat(m, "vis.curvatureScale", ui_curvatureScale);
		ui_curvatureThreshold= getFloat(m, "vis.curvatureThreshold", ui_curvatureThreshold);
		ui_curvatureStrength = getFloat(m, "vis.curvatureStrength", ui_curvatureStrength);
		ui_curvatureColor[0] = getFloat(m, "vis.curvatureColorR", ui_curvatureColor[0]);
		ui_curvatureColor[1] = getFloat(m, "vis.curvatureColorG", ui_curvatureColor[1]);
		ui_curvatureColor[2] = getFloat(m, "vis.curvatureColorB", ui_curvatureColor[2]);

		// Audio global
		ui_audioEnabled = getBool(m, "audio.enabled", ui_audioEnabled);
		ui_audioVolume  = getFloat(m, "audio.volume", ui_audioVolume);
		ui_audio_k      = getFloat(m, "audio.k", ui_audio_k);
		ui_audio_d      = getFloat(m, "audio.d", ui_audio_d);

		// Spectrum
		ui_showSpectrumWindow = getBool(m, "audio.showSpectrum", ui_showSpectrumWindow);
		ui_freezeSpectrum     = getBool(m, "audio.freezeSpectrum", ui_freezeSpectrum);
		ui_spectrumSmooth     = getFloat(m, "audio.spectrumSmooth", ui_spectrumSmooth);
		ui_spectrumMaxHz      = getFloat(m, "audio.spectrumMaxHz", ui_spectrumMaxHz);

		// Drivers
		int dcount = getInt(m, "drivers.count", 0);
		drivers.clear();
		drivers.reserve(std::max(0, dcount));
		for (int i = 0; i < dcount; ++i) {
			DriverSource d;
			std::string p = "driver." + std::to_string(i) + ".";
			d.name = getStr(m, (p + "name").c_str(), ("Driver " + std::to_string(i)).c_str());
			d.enabled = getBool(m, (p + "enabled").c_str(), true);
			d.type = (DriverType)getInt(m, (p + "type").c_str(), 0);
			d.pos01.x = getFloat(m, (p + "posx").c_str(), 0.5f);
			d.pos01.y = getFloat(m, (p + "posy").c_str(), 0.5f);
			d.width = getFloat(m, (p + "width").c_str(), 6.0f);
			d.amp = getFloat(m, (p + "amp").c_str(), 0.2f);

			d.oscOn = getBool(m, (p + "oscOn").c_str(), false);
			d.freqHz = getFloat(m, (p + "freqHz").c_str(), 1.0f);
			d.oscPhaseRad = getFloat(m, (p + "oscPhaseRad").c_str(), 0.0f);

			d.spinOn = getBool(m, (p + "spinOn").c_str(), false);
			d.spinHz = getFloat(m, (p + "spinHz").c_str(), 0.2f);
			d.spinPhaseRad = getFloat(m, (p + "spinPhaseRad").c_str(), 0.0f);
			d.orbitRadius01 = getFloat(m, (p + "orbitRadius01").c_str(), 0.0f);

			d.angleRad = getFloat(m, (p + "angleRad").c_str(), 0.0f);
			d.length01 = getFloat(m, (p + "length01").c_str(), 0.5f);

			drivers.push_back(d);
		}

		// Bands
		int bcount = getInt(m, "bands.count", 0);
		audioBands.clear();
		audioBands.reserve(std::max(0, bcount));
		for (int i = 0; i < bcount; ++i) {
			AudioBandSource b;
			std::string p = "band." + std::to_string(i) + ".";

			b.name = getStr(m, (p + "name").c_str(), ("Band " + std::to_string(i)).c_str());
			b.enabled = getBool(m, (p + "enabled").c_str(), true);
			b.type = (AudioBandType)getInt(m, (p + "type").c_str(), 0);
			b.mode = (AudioDriveMode)getInt(m, (p + "mode").c_str(), 0);

			b.fLowHz = getFloat(m, (p + "fLowHz").c_str(), 0.0f);
			b.fHighHz = getFloat(m, (p + "fHighHz").c_str(), 200.0f);

			b.pos01.x = getFloat(m, (p + "posx").c_str(), 0.5f);
			b.pos01.y = getFloat(m, (p + "posy").c_str(), 0.5f);

			b.gain = getFloat(m, (p + "gain").c_str(), 0.0f);
			b.threshold = getFloat(m, (p + "threshold").c_str(), 0.0f);
			b.radiusCells = getFloat(m, (p + "radiusCells").c_str(), 6.0f);
			b.attack = getFloat(m, (p + "attack").c_str(), 20.0f);
			b.release = getFloat(m, (p + "release").c_str(), 10.0f);

			b.agcEnabled = getBool(m, (p + "agcEnabled").c_str(), false);
			b.agcTimeSec = getFloat(m, (p + "agcTimeSec").c_str(), 0.8f);

			b.impulseGain = getFloat(m, (p + "impulseGain").c_str(), 0.0f);
			b.onsetThreshold = getFloat(m, (p + "onsetThreshold").c_str(), 0.2f);
			b.impulseCooldownSec = getFloat(m, (p + "impulseCooldownSec").c_str(), 0.08f);
			b.fluxSmoothRate = getFloat(m, (p + "fluxSmoothRate").c_str(), 20.0f);
			b.onsetHPTimeSec = getFloat(m, (p + "onsetHPTimeSec").c_str(), 0.35f);

			b.angleRad = getFloat(m, (p + "angleRad").c_str(), 0.0f);
			b.length01 = getFloat(m, (p + "length01").c_str(), 0.5f);

			// reset runtime-only state
			b.energyRaw = b.energyNorm = b.energySmoothed = 0.0f;
			b.fluxRaw = b.fluxSmoothed = 0.0f;
			b.cooldownTimer = 0.0f;
			b.prevTargetU = 0.0f;
			b.agcRunning = 0.0f;
			b.energySlow = 0.0f;

			audioBands.push_back(b);
		}

		selectedDriver = drivers.empty() ? -1 : std::clamp(selectedDriver, -1, (int)drivers.size() - 1);
		selectedAudioBand = audioBands.empty() ? -1 : std::clamp(selectedAudioBand, -1, (int)audioBands.size() - 1);
	};

	// ============================================================
	// UI setup
	// ============================================================
	WaterUI ui;
	WaterUIState uiState;

	uiState.waveSpeed = &ui_c;
	uiState.velDamp = &ui_velDamp;
	uiState.maxSlope = &ui_maxSlope;
	uiState.viscosity = &ui_viscosity;
	uiState.setLockWaterLevel = &ui_setLockWaterLevel;

	uiState.clickMag = &ui_clickMag;
	uiState.clickRadius = &ui_clickRadius;

	uiState.nearestHeight = &ui_nearestHeight;
	uiState.showNodes = &ui_showNodes;
	uiState.nodeEps = &ui_nodeEps;
	uiState.nodeStrength = &ui_nodeStrength;

	uiState.useHeightColoring = &ui_useHeightColoring;
	uiState.colorTheme = &ui_colorTheme;

	uiState.enableSpecular = &ui_enableSpecular;
	uiState.specularStrength = &ui_specularStrength;
	uiState.specularPower = &ui_specularPower;

	uiState.velocityEnabled = &ui_velocityEnabled;
	uiState.velocityScale = &ui_velocityScale;
	uiState.velocityThreshold = &ui_velocityThreshold;
	uiState.velocityStrength = &ui_velocityStrength;
	uiState.velocityColor = ui_velocityColor;

	uiState.slopeEnabled = &ui_slopeEnabled;
	uiState.slopeScale = &ui_slopeScale;
	uiState.slopeThreshold = &ui_slopeThreshold;
	uiState.slopeStrength = &ui_slopeStrength;
	uiState.slopeColor = ui_slopeColor;

	uiState.curvatureEnabled = &ui_curvatureEnabled;
	uiState.curvatureScale = &ui_curvatureScale;
	uiState.curvatureThreshold = &ui_curvatureThreshold;
	uiState.curvatureStrength = &ui_curvatureStrength;
	uiState.curvatureColor = ui_curvatureColor;

	uiState.openBoundary = &ui_useOpenBoundary;

	uiState.Nsim = &ui_Nsim;
	uiState.Nr = &ui_Nr;

	uiState.drivers = &drivers;
	uiState.selectedDriver = &selectedDriver;
	uiState.driverCounter = &driverCounter;

	uiState.setCameraTopDown = setCameraTopDown;
	uiState.setCameraDefault = setCameraDefault;
	uiState.resetSurface = [&]() { sim.reset(); };
	uiState.applyResolution = applyResolution;

	uiState.showDriverWindow = &show_driver_window;
	uiState.showAudioWindow = &show_audio_window;

	uiState.savePreset = [&]() {
		const char* path = tinyfd_saveFileDialog("Save preset", "watersim_preset.txt", 0, nullptr, nullptr);
		if (path) savePreset(path);
		};

	uiState.loadPreset = [&]() {
		const char* path = tinyfd_openFileDialog("Load preset", "", 0, nullptr, nullptr, 0);
		if (path) loadPreset(path);
		};


	audioState.sampleRate = &ui_audioSampleRate;
	audioState.spectrum = &audioAnalyzer.spectrum();

	audioState.enabled = &ui_audioEnabled;
	audioState.volume = &ui_audioVolume;

	audioState.loadedPath = &ui_audioPath;
	audioState.isLoaded = &ui_audioLoaded;
	audioState.isPlaying = &ui_audioPlaying;

	audioState.durationSec = &ui_audioDuration;
	audioState.cursorSec = &ui_audioCursor;

	audioState.seekSeconds = [&](float t) { audioEngine.seekSeconds(t); };

	audioState.onPlay = [&]() {
		if (!ui_audioEnabled) return;
		if (!audioEngine.isLoaded()) return;

		// Don't reset analyzer here. Let the transition logic handle clearing on stop/pause.
		audioEngine.play();
		};


	audioState.onPause = [&]() {
		audioEngine.pause();
		audioAnalyzer.reset();
		// no hard reset of bands, because you want gradual fade
		};


	audioState.onStop = [&]() {
		audioEngine.stop();

		// Clear spectrum so UI + any dependent logic goes to zero immediately
		audioAnalyzer.reset();

		// Prevent stop-spam impulses
		for (auto& b : audioBands) {
			b.cooldownTimer = 0.0f;
			b.fluxRaw = 0.0f;
		}
		};



	audioState.showSpectrumWindow = &ui_showSpectrumWindow;
	audioState.freezeSpectrum = &ui_freezeSpectrum;
	audioState.spectrumSmooth = &ui_spectrumSmooth;
	audioState.spectrumMaxHz = &ui_spectrumMaxHz;

	audioState.onLoadWav = [&]() {
		const char* filters[] = { "*.wav" };
		const char* path = tinyfd_openFileDialog("Open WAV", "", 1, filters, "WAV files", 0);
		if (path) {
			if (audioEngine.loadWav(path)) {
				ui_audioPath = path;
				ui_audioLoaded = true;
				ui_audioPlaying = false;
				audioEngine.setVolume(ui_audioVolume);
			}
		}
	};

	audioState.bands = &audioBands;
	audioState.selectedBand = &selectedAudioBand;
	audioState.bandCounter = &audioBandCounter;

	audioState.k_stiff = &ui_audio_k;
	audioState.d_damp = &ui_audio_d;

	bool wasPlaying = false;


	while (!glfwWindowShouldClose(window)) {
		// ---- Timing ----
		auto now = std::chrono::high_resolution_clock::now();
		auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last);
		float dt = std::chrono::duration<float>(dt_us).count();
		last = now;

		// Audio playback bookkeeping for UI
		audioEngine.setVolume(ui_audioVolume);
		ui_audioLoaded = audioEngine.isLoaded();
		ui_audioPlaying = audioEngine.isPlaying();
		ui_audioDuration = audioEngine.durationSeconds();
		ui_audioCursor = audioEngine.cursorSeconds();
		ui_audioSampleRate = audioEngine.sampleRate();

		simTime += dt;

		// FPS
		fps_accum_time += double(dt);
		fps_accum_frames += 1;
		if (fps_accum_time >= 1.0) {
			fps_display = float(double(fps_accum_frames) / fps_accum_time);
			fps_accum_time = 0.0;
			fps_accum_frames = 0;
		}

		// ---- Input/camera ----
		auto& io = ImGui::GetIO();
		inputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);

		glfwPollEvents();
		inputHandler.Advance();

		// If LMB held AND cursor is over water, block camera look
		bool blockCameraLook = false;
		{
			auto lmb = inputHandler.GetMouseState(GLFW_MOUSE_BUTTON_LEFT);
			if (!io.WantCaptureMouse && (lmb & PRESSED)) {
				int fbw, fbh;
				glfwGetFramebufferSize(window, &fbw, &fbh);

				double mx, my;
				glfwGetCursorPos(window, &mx, &my);

				glm::mat4 model_to_world(1.0f);
				glm::mat4 world_to_clip = mCamera.GetWorldToClipMatrix();

				int gx, gy;
				if (pickWaterGrid(mx, my, fbw, fbh, world_to_clip, model_to_world, SIZE, Nsim, gx, gy)) {
					blockCameraLook = true;
				}
			}
		}

		// capture mouse if either UI wants it OR we want to "paint water"
		inputHandler.SetUICapture(io.WantCaptureMouse || blockCameraLook, io.WantCaptureKeyboard);

		mCamera.Update(dt_us, inputHandler);

		// Hotkeys
		if (inputHandler.GetKeycodeState(GLFW_KEY_R) & JUST_PRESSED) {
			shader_reload_failed = !program_manager.ReloadAllPrograms();
			if (shader_reload_failed) {
				tinyfd_notifyPopup("Shader Program Reload Error",
					"An error occurred while reloading shader programs; see the logs for details.\n"
					"Rendering is suspended until the issue is solved. Once fixed, just reload the shaders again.",
					"error");
			}
		}
		if (inputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED) show_gui = !show_gui;
		if (inputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED) show_logs = !show_logs;
		if (inputHandler.GetKeycodeState(GLFW_KEY_F11) & JUST_RELEASED)
			mWindowManager.ToggleFullscreenStatusForWindow(window);

		// Viewport
		int framebuffer_width, framebuffer_height;
		glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
		glViewport(0, 0, framebuffer_width, framebuffer_height);

		mWindowManager.NewImGuiFrame();

		// Apply global sim params
		sim.setWaveSpeed(ui_c);
		sim.setVelDamp(ui_velDamp);
		sim.setMaxSlope(ui_maxSlope);
		sim.setOpenBoundary(ui_useOpenBoundary);
		sim.setViscosity(ui_viscosity);
		sim.setLockWaterLevel(ui_setLockWaterLevel);

		glm::mat4 model_to_world(1.0f);
		glm::mat4 world_to_clip = mCamera.GetWorldToClipMatrix();
		glm::mat4 normal_to_world = glm::transpose(glm::inverse(model_to_world));

		// ============================================================
		// 1) Mouse interaction: impulse on click + stable "finger press" while holding
		// ============================================================
		if (!io.WantCaptureMouse) {
			auto lmb = inputHandler.GetMouseState(GLFW_MOUSE_BUTTON_LEFT);

			if (lmb & (JUST_PRESSED | PRESSED)) {
				double mx, my;
				glfwGetCursorPos(window, &mx, &my);

				int gx = 0, gy = 0;
				if (pickWaterGrid(mx, my, framebuffer_width, framebuffer_height,
					world_to_clip, model_to_world, SIZE, Nsim, gx, gy))
				{
					// A1) Impulse ONLY on JUST_PRESSED (splash)
					if (lmb & JUST_PRESSED) {
						float impulse = ui_clickMag * 0.03f;
						sim.disturb(gx, gy, impulse, ui_clickRadius);
					}

					// A2) Stable finger press while holding (spring to target height)
					if (lmb & PRESSED) {
						float targetU = std::clamp(0.01f * ui_clickMag, -0.35f, 0.35f);
						const float k_press = 300.0f;
						const float d_press = 25.0f;

						sim.pullPointTargetHeight(
							gx, gy,
							dt,
							targetU,
							float(ui_clickRadius),
							k_press,
							d_press
						);
					}
				}
			}
		}

		// ============================================================
		// 2) Driver sources (multi)
		// ============================================================
		for (auto const& d : drivers) {
			if (!d.enabled) continue;

			float targetU = d.amp;
			if (d.oscOn) {
				targetU = d.amp * std::sin(2.0f * PI * d.freqHz * simTime + d.oscPhaseRad);
			}

			glm::vec2 p01 = d.pos01;

			if (d.spinOn && d.spinHz > 0.0f && d.orbitRadius01 > 0.0f) {
				float ang = 2.0f * PI * d.spinHz * simTime + d.spinPhaseRad;
				p01 += d.orbitRadius01 * glm::vec2(std::cos(ang), std::sin(ang));
			}

			p01.x = std::clamp(p01.x, 0.0f, 1.0f);
			p01.y = std::clamp(p01.y, 0.0f, 1.0f);

			const float gx = p01.x * float(Nsim - 1);
			const float gy = p01.y * float(Nsim - 1);

			const float k_stiff = 150.0f;
			const float d_damp  = 18.0f;

			if (d.type == DriverType::Point) {
				sim.pullPointTargetHeight(
					int(gx + 0.5f), int(gy + 0.5f),
					dt,
					targetU,
					d.width,
					k_stiff,
					d_damp
				);
			} else {
				float lineAngle = d.angleRad;
				if (d.spinOn && d.spinHz > 0.0f) {
					lineAngle += 2.0f * PI * d.spinHz * simTime + d.spinPhaseRad;
				}

				const float dirx = std::cos(lineAngle);
				const float diry = std::sin(lineAngle);

				const float halfMax = maxHalfLenToBoundsCells(gx, gy, dirx, diry, Nsim);
				const float halfLen = halfMax * std::clamp(d.length01, 0.0f, 1.0f);
				const float lengthCells = 2.0f * halfLen;

				sim.pullSegmentTargetHeight(
					gx, gy,
					dirx, diry,
					lengthCells,
					dt,
					targetU,
					d.width,
					k_stiff,
					d_damp
				);
			}
		}

		// ============================================================
		// Audio analysis update + apply audio bands
		// ============================================================
		const bool isLoaded = ui_audioEnabled && audioEngine.isLoaded();
		const bool isPlaying = isLoaded && audioEngine.isPlaying();

		// Transition detection
		if (wasPlaying && !isPlaying) {
			// We just stopped or paused: clear spectrum ONCE and start releasing
			audioAnalyzer.reset();

			// Also prevent any leftover "impulse cooldown" logic from firing later
			for (auto& b : audioBands) {
				b.cooldownTimer = 0.0f;
				b.fluxRaw = 0.0f;
			}
		}

		// Drive only while actually playing
		if (isPlaying) {
			const uint32_t sr = audioEngine.sampleRate();
			audioAnalyzer.update(dt, sr);

			for (auto& b : audioBands) {
				if (!b.enabled) continue;

				if (b.fHighHz < b.fLowHz) std::swap(b.fLowHz, b.fHighHz);

				float E = audioAnalyzer.getBandEnergy(b.fLowHz, b.fHighHz, sr);
				b.energyRaw = E;

				// --- Absolute silence gate (use RAW energy, not En) ---
				const float absFloorE = 1e-7f;   // tune: 1e-8 .. 1e-5 depending on your analyzer scale
				const float releaseFast = 12.0f;   // 1/s
				const float aFast = 1.0f - std::exp(-releaseFast * dt);

				if (E < absFloorE) {
					// Treat as true silence: remove ALL driving smoothly
					b.energyNorm = 0.0f;
					b.energySmoothed = b.energySmoothed + aFast * (0.0f - b.energySmoothed);
					b.prevTargetU = b.prevTargetU + aFast * (0.0f - b.prevTargetU);

					b.energySlow = b.energySlow + aFast * (0.0f - b.energySlow);
					b.fluxRaw = 0.0f;
					b.fluxSmoothed = b.fluxSmoothed + aFast * (0.0f - b.fluxSmoothed);
					b.cooldownTimer = 0.0f;

					if (b.agcEnabled) {
						b.agcRunning = b.agcRunning + aFast * (0.0f - b.agcRunning);
					}

					continue; // no impulse, no envelope
				}

				// --- AGC / normalization (safe) ---
				float En = E;
				if (b.agcEnabled) {
					float T = std::max(0.05f, b.agcTimeSec);
					float alpha = 1.0f - std::exp(-dt / T);
					b.agcRunning = b.agcRunning + alpha * (E - b.agcRunning);

					const float agcMinDenom = 1e-4f; // tune: 1e-5..1e-3
					float denom = std::max(std::max(b.agcRunning, b.agcFloor), agcMinDenom);
					En = E / denom;
				}
				b.energyNorm = En;


				// Envelope
				// Envelope input
				float x = En - b.threshold;
				if (x < 0.0f) x = 0.0f;

				// --- Envelope deadzone (kills quiet jitter bulge) ---
				const float envDeadzone = 0.003f;   // try 0.001 .. 0.02
				if (x < envDeadzone) x = 0.0f;

				float rate = (x > b.energySmoothed) ? b.attack : b.release;
				float aEnv = 1.0f - std::exp(-rate * dt);
				b.energySmoothed = b.energySmoothed + aEnv * (x - b.energySmoothed);

				// If envelope effectively off, force target back to zero
				const float envOffEps = 1e-4f;
				if (b.energySmoothed < envOffEps) {
					b.energySmoothed = 0.0f;
					b.prevTargetU += (1.0f - std::exp(-12.0f * dt)) * (0.0f - b.prevTargetU);
				}


				// Flux/onset
				float Thp = std::max(0.02f, b.onsetHPTimeSec);
				float aSlow = 1.0f - std::exp(-dt / Thp);
				b.energySlow = b.energySlow + aSlow * (En - b.energySlow);

				float flux = std::max(0.0f, En - b.energySlow);
				b.fluxRaw = flux;

				float aFlux = 1.0f - std::exp(-std::max(0.0f, b.fluxSmoothRate) * dt);
				b.fluxSmoothed = b.fluxSmoothed + aFlux * (flux - b.fluxSmoothed);

				b.cooldownTimer = std::max(0.0f, b.cooldownTimer - dt);

				// Position
				float gx_f = b.pos01.x * float(Nsim - 1);
				float gy_f = b.pos01.y * float(Nsim - 1);
				int gx = std::clamp(int(gx_f + 0.5f), 1, Nsim - 2);
				int gy = std::clamp(int(gy_f + 0.5f), 1, Nsim - 2);

				// Impulse path (only while playing)
				const bool doImpulse = (b.mode == AudioDriveMode::Impulse || b.mode == AudioDriveMode::Hybrid);
				if (doImpulse && (b.cooldownTimer <= 0.0f) && (b.fluxSmoothed > b.onsetThreshold)) {
					float mag = b.impulseGain * b.fluxSmoothed;
					mag = 50.0f * std::tanh(mag / 50.0f);

					int r = std::clamp(int(std::round(b.radiusCells)), 1, 80);

					if (b.type == AudioBandType::Point) {
						sim.disturb(gx, gy, mag, r);
					}
					else {
						float dirx = std::cos(b.angleRad);
						float diry = std::sin(b.angleRad);

						float halfMax = maxHalfLenToBoundsCells(gx_f, gy_f, dirx, diry, Nsim);
						float halfLen = halfMax * std::clamp(b.length01, 0.0f, 1.0f);
						float lengthCells = 2.0f * halfLen;

						int samples = int(std::ceil(lengthCells / std::max(1.0f, 0.6f * float(r)))) + 1;
						samples = std::clamp(samples, 9, 256);

						float magPer = mag / float(samples);
						for (int i = 0; i < samples; ++i) {
							float t = (samples == 1) ? 0.0f : (float(i) / float(samples - 1));
							float s = (t - 0.5f) * lengthCells;
							int xi = std::clamp(int(gx_f + s * dirx + 0.5f), 1, Nsim - 2);
							int yi = std::clamp(int(gy_f + s * diry + 0.5f), 1, Nsim - 2);
							sim.disturb(xi, yi, magPer, r);
						}
					}

					b.cooldownTimer = std::max(0.0f, b.impulseCooldownSec);
				}

				// Envelope path (only while playing)
				const bool doEnvelope = (b.mode == AudioDriveMode::Envelope || b.mode == AudioDriveMode::Hybrid);
				if (!doEnvelope) {
					b.prevTargetU = 0.0f;
					continue;
				}

				float desired = b.gain * b.energySmoothed;
				desired = 0.5f * std::tanh(desired / 0.5f);

				float maxDeltaU = 0.25f;
				float delta = std::clamp(desired - b.prevTargetU, -maxDeltaU, maxDeltaU);
				float targetU = b.prevTargetU + delta;
				b.prevTargetU = targetU;

				if (b.type == AudioBandType::Point) {
					sim.pullPointTargetHeight(gx, gy, dt, targetU, b.radiusCells, ui_audio_k, ui_audio_d);
				}
				else {
					float dirx = std::cos(b.angleRad);
					float diry = std::sin(b.angleRad);

					float halfMax = maxHalfLenToBoundsCells(gx_f, gy_f, dirx, diry, Nsim);
					float halfLen = halfMax * std::clamp(b.length01, 0.0f, 1.0f);
					float lengthCells = 2.0f * halfLen;

					sim.pullSegmentTargetHeight(gx_f, gy_f, dirx, diry, lengthCells, dt, targetU, b.radiusCells, ui_audio_k, ui_audio_d);
				}
			}
		}
		else {
			// Not playing: smoothly release all band influence (no forcing)
			releaseAudioBands(dt);
		}

		wasPlaying = isPlaying;



		// ============================================================
		// 3) Sim step + render
		// ============================================================
		sim.update(dt);

		renderer.setHeightFiltering(ui_nearestHeight);
		renderer.updateHeightTexture(sim.packedHV(), sim.N());

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::vec3 light_dir_ws = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.2f));
		glm::vec3 cam_pos_ws = mCamera.mWorld.GetTranslation();

		vis.showNodes = ui_showNodes;
		vis.nodeEps = ui_nodeEps;
		vis.nodeStrength = ui_nodeStrength;

		vis.useHeightColoring = ui_useHeightColoring;
		vis.colorTheme = ui_colorTheme;

		vis.enableSpecular = ui_enableSpecular;
		vis.specularStrength = ui_specularStrength;
		vis.specularPower = ui_specularPower;

		vis.velocityEnabled = ui_velocityEnabled;
		vis.velocityScale = ui_velocityScale;
		vis.velocityThreshold = ui_velocityThreshold;
		vis.velocityStrength = ui_velocityStrength;
		vis.velocityColor[0] = ui_velocityColor[0];
		vis.velocityColor[1] = ui_velocityColor[1];
		vis.velocityColor[2] = ui_velocityColor[2];

		vis.slopeEnabled = ui_slopeEnabled;
		vis.slopeScale = ui_slopeScale;
		vis.slopeThreshold = ui_slopeThreshold;
		vis.slopeStrength = ui_slopeStrength;
		vis.slopeColor[0] = ui_slopeColor[0];
		vis.slopeColor[1] = ui_slopeColor[1];
		vis.slopeColor[2] = ui_slopeColor[2];

		vis.curvatureEnabled = ui_curvatureEnabled;
		vis.curvatureScale = ui_curvatureScale;
		vis.curvatureThreshold = ui_curvatureThreshold;
		vis.curvatureStrength = ui_curvatureStrength;
		vis.curvatureColor[0] = ui_curvatureColor[0];
		vis.curvatureColor[1] = ui_curvatureColor[1];
		vis.curvatureColor[2] = ui_curvatureColor[2];

		renderer.render(
			water_shader,
			glm::value_ptr(world_to_clip),
			glm::value_ptr(model_to_world),
			glm::value_ptr(normal_to_world),
			glm::value_ptr(light_dir_ws),
			glm::value_ptr(cam_pos_ws),
			SIZE,
			sim.dx(),
			vis
		);

		// ============================================================
		// GUI
		// ============================================================
		if (show_gui) {
			ui.drawMain(uiState, Nsim, Nr, fps_display);

			if (show_driver_window) {
				ui.drawDriversWindow(uiState);
			}
			if (show_audio_window) {
				audioUi.draw(audioState);
				audioUi.drawSpectrumWindow(audioState);
			}
		}

		if (show_logs)
			Log::View::Render();

		mWindowManager.RenderImGuiFrame(show_gui);
		glfwSwapBuffers(window);
	}

	glDeleteProgram(water_shader);
}
