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

// --- Refactor: presets + settings ---
#include "WaterSettings.hpp"
#include "PresetIO.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tinyfiledialogs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>


static constexpr float PI = 3.14159265358979323846f;

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
	glm::vec4 pFar = clip_to_world * glm::vec4(x, y, 1.0f, 1.0f);
	pNear /= pNear.w;
	pFar /= pFar.w;

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

	// ---- Settings (persisted) ----
	WaterSettings settings;

	int Nsim = settings.res.Nsim; // simulation resolution
	int Nr = settings.res.Nr;   // render resolution

	WaterHeightfield sim(Nsim, SIZE);
	WaterMesh        mesh(Nr, SIZE);
	WaterRenderer    renderer;

	renderer.setMesh(mesh.vertexData(), mesh.indices());
	renderer.updateHeightTexture(sim.packedHV(), sim.N());

	// Keep defaults in sync with actual sim defaults (in case WaterHeightfield differs)
	settings.sim.c = sim.waveSpeed();
	settings.sim.velDamp = sim.velDamp();
	settings.sim.maxSlope = sim.maxSlope();
	settings.sim.viscosity = sim.viscosity();
	settings.sim.openBoundary = sim.isOpenBoundary();
	// settings.sim.lockWaterLevel stays from struct default unless you want to mirror sim

	// Apply resolution changes (recreates sim+mesh, updates renderer)
	auto applyResolution = [&](int newNsim, int newNr)
		{
			newNsim = std::clamp(newNsim, 16, 512);
			newNr = std::clamp(newNr, 32, 2048);

			Nsim = newNsim;
			Nr = newNr;

			settings.res.Nsim = Nsim;
			settings.res.Nr = Nr;

			sim = WaterHeightfield(Nsim, SIZE);
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

	// Mirror these to settings.win (so preset saves them)
	settings.win.showDriverWindow = settings.win.showDriverWindow;
	settings.win.showAudioWindow = settings.win.showAudioWindow;

	// ---- Audio bands release helper (uses settings.audio.bands) ----
	auto releaseAudioBands = [&](float dt)
		{
			const float envReleasePerSec = 6.0f;
			const float fluxReleasePerSec = 10.0f;
			const float agcReleasePerSec = 2.0f;

			float aEnv = 1.0f - std::exp(-envReleasePerSec * dt);
			float aFlux = 1.0f - std::exp(-fluxReleasePerSec * dt);
			float aAgc = 1.0f - std::exp(-agcReleasePerSec * dt);

			for (auto& b : settings.audio.bands) {
				b.cooldownTimer = 0.0f;
				b.fluxRaw = 0.0f;

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

	// ---- Audio system ----
	AudioAnalyzer audioAnalyzer;
	AudioEngine   audioEngine;
	audioEngine.setAnalyzer(&audioAnalyzer);

	// Playback state mirror (not persisted)
	bool        ui_audioLoaded = false;
	bool        ui_audioPlaying = false;
	std::string ui_audioPath;
	float       ui_audioDuration = 0.0f;
	float       ui_audioCursor = 0.0f;
	uint32_t    ui_audioSampleRate = 0;

	// Visual params bundle (persisted inside settings.render.vis)
	WaterVisualParams vis;

	// Drivers (persisted in settings.drivers)
	// NOTE: selection/counter persisted in settings.{selectedDriver,driverCounter}
	// Bands (persisted in settings.audio.{bands,selectedBand,bandCounter})

	// FPS tracking
	float  fps_display = 0.0f;
	double fps_accum_time = 0.0;
	int    fps_accum_frames = 0;

	auto last = std::chrono::high_resolution_clock::now();
	bool shader_reload_failed = false;

	float simTime = 0.0f;

	// Preset load deferral (avoid tinyfd buffer lifetime issues)
	std::string pendingPresetPath;
	bool pendingPresetApply = false;

	// ============================================================
	// UI setup (NEW refactor style)
	// ============================================================
	WaterUI ui;
	WaterUIState uiState;
	uiState.settings = &settings;

	uiState.setCameraTopDown = setCameraTopDown;
	uiState.setCameraDefault = setCameraDefault;
	uiState.resetSurface = [&]() { sim.reset(); };
	uiState.applyResolution = applyResolution;

	uiState.savePreset = [&]() {
		const char* path = tinyfd_saveFileDialog(
			"Save preset", "watersim_preset.txt", 0, nullptr, nullptr);
		if (!path) return;
		PresetIO::Save(path, settings);
		};

	uiState.loadPreset = [&]() {
		const char* path = tinyfd_openFileDialog(
			"Load preset", "", 0, nullptr, nullptr, 0);
		if (!path) return;
		pendingPresetPath = path;     // copy it
		pendingPresetApply = true;    // apply next frame
		};


	// ============================================================
	// Audio UI wiring (NEW refactor style)
	// ============================================================
	AudioUI audioUi;
	AudioUIState audioState;
	audioState.settings = &settings;

	audioState.durationSec = &ui_audioDuration;
	audioState.cursorSec = &ui_audioCursor;
	audioState.seekSeconds = [&](float t) { audioEngine.seekSeconds(t); };

	audioState.onLoadWav = [&]() {
		const char* filters[] = { "*.wav" };
		const char* path = tinyfd_openFileDialog("Open WAV", "", 1, filters, "WAV files", 0);
		if (!path) return;

		if (audioEngine.loadWav(path)) {
			ui_audioPath = path;
			ui_audioLoaded = true;
			ui_audioPlaying = false;
			audioEngine.setVolume(settings.audio.volume);
		}
		};

	audioState.onPlay = [&]() {
		if (!settings.audio.enabled) return;
		if (!audioEngine.isLoaded()) return;
		audioEngine.play();
		};

	audioState.onPause = [&]() {
		audioEngine.pause();
		audioAnalyzer.reset();
		};

	audioState.onStop = [&]() {
		audioEngine.stop();
		audioAnalyzer.reset();
		for (auto& b : settings.audio.bands) {
			b.cooldownTimer = 0.0f;
			b.fluxRaw = 0.0f;
		}
		};

	audioState.loadedPath = &ui_audioPath;
	audioState.isLoaded = &ui_audioLoaded;
	audioState.isPlaying = &ui_audioPlaying;

	audioState.sampleRate = &ui_audioSampleRate;
	audioState.spectrum = audioAnalyzer.spectrumPtr();

	bool wasPlaying = false;

	// ============================================================
	// Main loop
	// ============================================================
	while (!glfwWindowShouldClose(window)) {
		// ---- Timing ----
		auto now = std::chrono::high_resolution_clock::now();
		auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last);
		float dt = std::chrono::duration<float>(dt_us).count();
		last = now;

		// Audio playback bookkeeping for UI
		audioEngine.setVolume(settings.audio.volume);
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
			if (!io.WantCaptureMouse && (lmb & (PRESSED | JUST_PRESSED)))
			{
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

		// Deferred preset apply (so we don't fight ImGui state mid-frame)
		if (pendingPresetApply) {
			int oldNsim = Nsim;
			int oldNr = Nr;

			if (PresetIO::Load(pendingPresetPath.c_str(), settings)) {
				// resolution changes require recreation
				if (settings.res.Nsim != oldNsim || settings.res.Nr != oldNr) {
					applyResolution(settings.res.Nsim, settings.res.Nr);
				}

				// Clamp selection safety (in case older presets)
				if (settings.drivers.empty()) settings.selectedDriver = -1;
				else if (settings.selectedDriver < 0 || settings.selectedDriver >= (int)settings.drivers.size()) settings.selectedDriver = 0;

				if (settings.audio.bands.empty()) settings.audio.selectedBand = -1;
				else if (settings.audio.selectedBand < 0 || settings.audio.selectedBand >= (int)settings.audio.bands.size()) settings.audio.selectedBand = 0;

				settings.driverCounter = std::max(settings.driverCounter, (int)settings.drivers.size() + 1);
				settings.audio.bandCounter = std::max(settings.audio.bandCounter, (int)settings.audio.bands.size() + 1);

			}

			pendingPresetApply = false;

			ImGui::SetKeyboardFocusHere(-1);
			ImGui::GetIO().WantCaptureKeyboard = false;
			ImGui::GetIO().WantCaptureMouse = false;
		}



		// Apply global sim params (from settings)
		sim.setWaveSpeed(settings.sim.c);
		sim.setVelDamp(settings.sim.velDamp);
		sim.setMaxSlope(settings.sim.maxSlope);
		sim.setOpenBoundary(settings.sim.openBoundary);
		sim.setViscosity(settings.sim.viscosity);
		sim.setLockWaterLevel(settings.sim.lockWaterLevel);

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
						float impulse = settings.mouse.clickMag * 0.03f;
						sim.disturb(gx, gy, impulse, settings.mouse.clickRadius);
					}

					// A2) Stable finger press while holding (spring to target height)
					if (lmb & PRESSED) {
						float targetU = std::clamp(0.01f * settings.mouse.clickMag, -0.35f, 0.35f);
						const float k_press = 300.0f;
						const float d_press = 25.0f;

						sim.pullPointTargetHeight(
							gx, gy,
							dt,
							targetU,
							float(settings.mouse.clickRadius),
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
		for (auto const& d : settings.drivers) {
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
			const float d_damp = 18.0f;

			if (d.type == DriverType::Point) {
				sim.pullPointTargetHeight(
					int(gx + 0.5f), int(gy + 0.5f),
					dt,
					targetU,
					d.width,
					k_stiff,
					d_damp
				);
			}
			else {
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
		const bool isLoaded = settings.audio.enabled && audioEngine.isLoaded();
		const bool isPlaying = isLoaded && audioEngine.isPlaying();

		// Transition detection
		if (wasPlaying && !isPlaying) {
			audioAnalyzer.reset();

			for (auto& b : settings.audio.bands) {
				b.cooldownTimer = 0.0f;
				b.fluxRaw = 0.0f;
			}
		}

		// Drive only while actually playing
		if (isPlaying) {
			const uint32_t sr = audioEngine.sampleRate();
			audioAnalyzer.update(dt, sr);

			for (auto& b : settings.audio.bands) {
				if (!b.enabled) continue;

				if (b.fHighHz < b.fLowHz) std::swap(b.fLowHz, b.fHighHz);

				float E = audioAnalyzer.getBandEnergy(b.fLowHz, b.fHighHz, sr);
				b.energyRaw = E;

				// --- Absolute silence gate (use RAW energy, not En) ---
				const float absFloorE = 1e-7f;
				const float releaseFast = 12.0f;
				const float aFast = 1.0f - std::exp(-releaseFast * dt);

				if (E < absFloorE) {
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

					continue;
				}

				// --- AGC / normalization (safe) ---
				float En = E;
				if (b.agcEnabled) {
					float T = std::max(0.05f, b.agcTimeSec);
					float alpha = 1.0f - std::exp(-dt / T);
					b.agcRunning = b.agcRunning + alpha * (E - b.agcRunning);

					const float agcMinDenom = 1e-4f;
					float denom = std::max(std::max(b.agcRunning, b.agcFloor), agcMinDenom);
					En = E / denom;
				}
				b.energyNorm = En;

				// Envelope
				float x = En - b.threshold;
				if (x < 0.0f) x = 0.0f;

				const float envDeadzone = 0.003f;
				if (x < envDeadzone) x = 0.0f;

				float rate = (x > b.energySmoothed) ? b.attack : b.release;
				float aEnv = 1.0f - std::exp(-rate * dt);
				b.energySmoothed = b.energySmoothed + aEnv * (x - b.energySmoothed);

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
					float xImp = b.fluxSmoothed - b.onsetThreshold;
					xImp /= std::max(1e-6f, (1.0f - b.onsetThreshold));
					xImp = std::clamp(xImp, 0.0f, 1.0f);
					xImp = xImp * xImp;

					float mag = b.impulseGain * xImp;
					mag = 20.0f * std::tanh(mag / 20.0f);

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

				const float maxU = 1.2f;
				desired = maxU * std::tanh(desired / maxU);

				float maxDeltaU = 0.6f;

				float delta = std::clamp(desired - b.prevTargetU, -maxDeltaU, maxDeltaU);
				float targetU = b.prevTargetU + delta;
				b.prevTargetU = targetU;

				if (b.type == AudioBandType::Point) {
					sim.pullPointTargetHeight(gx, gy, dt, targetU, b.radiusCells, settings.audio.k_stiff, settings.audio.d_damp);
				}
				else {
					float dirx = std::cos(b.angleRad);
					float diry = std::sin(b.angleRad);

					float halfMax = maxHalfLenToBoundsCells(gx_f, gy_f, dirx, diry, Nsim);
					float halfLen = halfMax * std::clamp(b.length01, 0.0f, 1.0f);
					float lengthCells = 2.0f * halfLen;

					sim.pullSegmentTargetHeight(gx_f, gy_f, dirx, diry, lengthCells, dt, targetU, b.radiusCells, settings.audio.k_stiff, settings.audio.d_damp);
				}
			}
		}
		else {
			releaseAudioBands(dt);
		}

		wasPlaying = isPlaying;

		// ============================================================
		// 3) Sim step + render
		// ============================================================
		sim.update(dt);

		renderer.setHeightFiltering(settings.render.nearestHeight);
		renderer.updateHeightTexture(sim.packedHV(), sim.N());

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::vec3 light_dir_ws = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.2f));
		glm::vec3 cam_pos_ws = mCamera.mWorld.GetTranslation();

		// vis is persisted in settings.render.vis; copy into the struct passed to renderer
		vis = settings.render.vis;

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

			if (settings.win.showDriverWindow) {
				ui.drawDriversWindow(uiState);
			}
			if (settings.win.showAudioWindow) {
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
