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

#include "AudioEngine.hpp"
#include "AudioAnalyzer.hpp"
#include "AudioUI.hpp"

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

// Mouse to (gx,gy) on the water grid. Used for clicking/dragging + camera block.
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

	// Mouse px -> NDC
	float x = (2.0f * float(mouseX) / float(screenW)) - 1.0f;
	float y = 1.0f - (2.0f * float(mouseY) / float(screenH));

	// Unproject to a world ray
	glm::vec4 pNear = clip_to_world * glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 pFar = clip_to_world * glm::vec4(x, y, 1.0f, 1.0f);
	pNear /= pNear.w;
	pFar /= pFar.w;

	glm::vec3 ro = glm::vec3(pNear);
	glm::vec3 rd = glm::normalize(glm::vec3(pFar - pNear));

	// Intersect with the water plane
	float planeY = (model_to_world * glm::vec4(0, 0, 0, 1)).y;

	const float denom = rd.y;
	if (std::abs(denom) < 1e-6f) return false;

	float t = (planeY - ro.y) / denom;
	if (t < 0.0f) return false;

	glm::vec3 hit = ro + t * rd;

	// World hit -> water local -> uv -> grid index
	glm::mat4 world_to_model = glm::inverse(model_to_world);
	glm::vec3 hitLocal = glm::vec3(world_to_model * glm::vec4(hit, 1.0f));

	float half = 0.5f * waterSize;
	float u = (hitLocal.x + half) / waterSize;
	float v = (hitLocal.z + half) / waterSize;

	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return false;

	outGX = int(u * float(N - 1) + 0.5f);
	outGY = int(v * float(N - 1) + 0.5f);

	// Keep off borders so neighbor ops are safe
	outGX = std::clamp(outGX, 1, N - 2);
	outGY = std::clamp(outGY, 1, N - 2);

	return true;
}

// For line bands: max half-length (cells) you can extend from (cx,cy) along dir without leaving the grid.
static float maxHalfLenToBoundsCells(float cx, float cy, float dirx, float diry, int N)
{
	const float xmin = 0.0f, xmax = float(N - 1);
	const float ymin = 0.0f, ymax = float(N - 1);

	auto safeDiv = [](float a, float b) {
		return (std::abs(b) > 1e-6f) ? (a / b) : std::numeric_limits<float>::infinity();
		};

	// forward
	float t1 = safeDiv(xmin - cx, dirx);
	float t2 = safeDiv(xmax - cx, dirx);
	float t3 = safeDiv(ymin - cy, diry);
	float t4 = safeDiv(ymax - cy, diry);

	float tPos = std::numeric_limits<float>::infinity();
	if (t1 > 0.0f) tPos = std::min(tPos, t1);
	if (t2 > 0.0f) tPos = std::min(tPos, t2);
	if (t3 > 0.0f) tPos = std::min(tPos, t3);
	if (t4 > 0.0f) tPos = std::min(tPos, t4);

	// backward
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


// ProjectWater
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

// Main loop
void edan35::ProjectWater::run()
{
	// Camera + GL
	mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 2.0f, 4.0f));
	mCamera.mWorld.SetRotateX(-0.5f);
	mCamera.mMouseSensitivity = glm::vec2(0.003f);
	mCamera.mMovementSpeed = glm::vec3(3.0f);

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

	// Sim + renderer
	const float SIZE = 4.0f;

	WaterSettings settings;

	int Nsim = settings.res.Nsim;
	int Nr = settings.res.Nr;

	WaterHeightfield sim(Nsim, SIZE);
	WaterMesh        mesh(Nr, SIZE);
	WaterRenderer    renderer;

	renderer.setMesh(mesh.vertexData(), mesh.indices());
	renderer.updateHeightTexture(sim.packedHV(), sim.N());

	// Mirror sim defaults into settings
	settings.sim.c = sim.waveSpeed();
	settings.sim.velDamp = sim.velDamp();
	settings.sim.maxSlope = sim.maxSlope();
	settings.sim.viscosity = sim.viscosity();
	settings.sim.openBoundary = sim.isOpenBoundary();

	auto applyResolution = [&](int newNsim, int newNr) {
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

	auto applySimSettings = [&]() {
		sim.setWaveSpeed(settings.sim.c);
		sim.setVelDamp(settings.sim.velDamp);
		sim.setMaxSlope(settings.sim.maxSlope);
		sim.setOpenBoundary(settings.sim.openBoundary);
		sim.setViscosity(settings.sim.viscosity);
		sim.setLockWaterLevel(settings.sim.lockWaterLevel);
		};

	auto applyRenderSettings = [&]() {
		renderer.setHeightFiltering(settings.render.nearestHeight);
		};

	// Shader
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

	// Audio runtime + UI mirror
	AudioAnalyzer audioAnalyzer;
	AudioEngine   audioEngine;
	audioEngine.setAnalyzer(&audioAnalyzer);

	bool        ui_audioLoaded = false;
	bool        ui_audioPlaying = false;
	std::string ui_audioPath;
	float       ui_audioDuration = 0.0f;
	float       ui_audioCursor = 0.0f;
	uint32_t    ui_audioSampleRate = 0;

	auto syncAudioUiState = [&]() {
		audioEngine.setVolume(settings.audio.volume);

		ui_audioLoaded = audioEngine.isLoaded();
		ui_audioPlaying = audioEngine.isPlaying();
		ui_audioDuration = audioEngine.durationSeconds();
		ui_audioCursor = audioEngine.cursorSeconds();
		ui_audioSampleRate = audioEngine.sampleRate();
		};

	auto releaseAudioBands = [&](float dt) {
		const float envReleasePerSec = 6.0f;
		const float agcReleasePerSec = 2.0f;

		const float aEnv = 1.0f - std::exp(-envReleasePerSec * dt);
		const float aAgc = 1.0f - std::exp(-agcReleasePerSec * dt);

		for (auto& b : settings.audio.bands) {
			b.energyRaw = 0.0f;
			b.energyNorm = 0.0f;
			b.energySmoothed = b.energySmoothed + aEnv * (0.0f - b.energySmoothed);
			b.prevTargetU = b.prevTargetU + aEnv * (0.0f - b.prevTargetU);

			if (b.agcEnabled) {
				b.agcRunning = b.agcRunning + aAgc * (0.0f - b.agcRunning);
			}
		}
		};

	// UI wiring
	bool show_gui = true;
	bool show_logs = false;

	std::string pendingPresetPath;
	bool pendingPresetApply = false;

	WaterUI ui;
	WaterUIState uiState;
	uiState.settings = &settings;

	uiState.setCameraTopDown = setCameraTopDown;
	uiState.setCameraDefault = setCameraDefault;
	uiState.resetSurface = [&]() { sim.reset(); };
	uiState.applyResolution = applyResolution;

	uiState.savePreset = [&]() {
		const char* path = tinyfd_saveFileDialog("Save preset", "watersim_preset.txt", 0, nullptr, nullptr);
		if (!path) return;
		PresetIO::Save(path, settings);
		};

	uiState.loadPreset = [&]() {
		const char* path = tinyfd_openFileDialog("Load preset", "", 0, nullptr, nullptr, 0);
		if (!path) return;
		pendingPresetPath = path;
		pendingPresetApply = true;
		};

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
		};

	audioState.loadedPath = &ui_audioPath;
	audioState.isLoaded = &ui_audioLoaded;
	audioState.isPlaying = &ui_audioPlaying;
	audioState.sampleRate = &ui_audioSampleRate;
	audioState.spectrum = audioAnalyzer.spectrumPtr();

	bool wasPlaying = false;

	// Timing / FPS
	float  fps_display = 0.0f;
	double fps_accum_time = 0.0;
	int    fps_accum_frames = 0;

	auto last = std::chrono::high_resolution_clock::now();

	// Main loop
	while (!glfwWindowShouldClose(window))
	{
		// Timing + events
		auto now = std::chrono::high_resolution_clock::now();
		auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last);
		float dt = std::chrono::duration<float>(dt_us).count();
		last = now;

		fps_accum_time += double(dt);
		fps_accum_frames += 1;
		if (fps_accum_time >= 1.0) {
			fps_display = float(double(fps_accum_frames) / fps_accum_time);
			fps_accum_time = 0.0;
			fps_accum_frames = 0;
		}

		auto& io = ImGui::GetIO();
		inputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);

		glfwPollEvents();
		inputHandler.Advance();

		// Frame setup
		int framebuffer_width, framebuffer_height;
		glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
		glViewport(0, 0, framebuffer_width, framebuffer_height);

		mWindowManager.NewImGuiFrame();

		// Hotkeys
		if (inputHandler.GetKeycodeState(GLFW_KEY_R) & JUST_PRESSED) {
			const bool ok = program_manager.ReloadAllPrograms();
			if (!ok) {
				tinyfd_notifyPopup(
					"Shader Program Reload Error",
					"An error occurred while reloading shader programs; see the logs for details.\n"
					"Fix then press R again.",
					"error");
			}
		}
		if (inputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED) show_gui = !show_gui;
		if (inputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED) show_logs = !show_logs;
		if (inputHandler.GetKeycodeState(GLFW_KEY_F11) & JUST_RELEASED)
			mWindowManager.ToggleFullscreenStatusForWindow(window);

		// Camera (block look when painting)
		bool blockCameraLook = false;
		if (!io.WantCaptureMouse) {
			auto lmb = inputHandler.GetMouseState(GLFW_MOUSE_BUTTON_LEFT);
			if (lmb & (PRESSED | JUST_PRESSED)) {
				double mx, my;
				glfwGetCursorPos(window, &mx, &my);

				glm::mat4 model_to_world(1.0f);
				glm::mat4 world_to_clip = mCamera.GetWorldToClipMatrix();

				int gx, gy;
				if (pickWaterGrid(mx, my, framebuffer_width, framebuffer_height,
					world_to_clip, model_to_world, SIZE, Nsim, gx, gy)) {
					blockCameraLook = true;
				}
			}
		}

		inputHandler.SetUICapture(io.WantCaptureMouse || blockCameraLook, io.WantCaptureKeyboard);
		mCamera.Update(dt_us, inputHandler);

		// Preset apply
		if (pendingPresetApply) {
			const int oldNsim = Nsim;
			const int oldNr = Nr;

			if (PresetIO::Load(pendingPresetPath.c_str(), settings)) {
				if (settings.res.Nsim != oldNsim || settings.res.Nr != oldNr) {
					applyResolution(settings.res.Nsim, settings.res.Nr);
				}

				if (settings.audio.bands.empty()) {
					settings.audio.selectedBand = -1;
				}
				else {
					settings.audio.selectedBand = std::clamp(
						settings.audio.selectedBand, 0, int(settings.audio.bands.size()) - 1);
				}
				settings.audio.bandCounter = std::max(
					settings.audio.bandCounter, int(settings.audio.bands.size()) + 1);

				applySimSettings();
				applyRenderSettings();
				audioEngine.setVolume(settings.audio.volume);
			}

			pendingPresetApply = false;
			io.WantCaptureKeyboard = false;
			io.WantCaptureMouse = false;
		}

		// Apply settings
		applySimSettings();
		applyRenderSettings();
		syncAudioUiState();

		glm::mat4 model_to_world(1.0f);
		glm::mat4 world_to_clip = mCamera.GetWorldToClipMatrix();
		glm::mat4 normal_to_world = glm::transpose(glm::inverse(model_to_world));

		// Mouse press
		if (!io.WantCaptureMouse) {
			auto lmb = inputHandler.GetMouseState(GLFW_MOUSE_BUTTON_LEFT);

			if (lmb & (JUST_PRESSED | PRESSED)) {
				double mx, my;
				glfwGetCursorPos(window, &mx, &my);

				int gx = 0, gy = 0;
				if (pickWaterGrid(mx, my, framebuffer_width, framebuffer_height,
					world_to_clip, model_to_world, SIZE, Nsim, gx, gy))
				{
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

		// Audio bands
		const bool audioLoaded = settings.audio.enabled && audioEngine.isLoaded();
		const bool audioPlaying = audioLoaded && audioEngine.isPlaying();

		if (wasPlaying && !audioPlaying) {
			audioAnalyzer.reset();
		}

		if (audioPlaying) {
			const uint32_t sr = audioEngine.sampleRate();
			audioAnalyzer.update(dt, sr);

			for (auto& b : settings.audio.bands) {
				if (!b.enabled) continue;
				if (b.fHighHz < b.fLowHz) std::swap(b.fLowHz, b.fHighHz);

				const float E = audioAnalyzer.getBandEnergy(b.fLowHz, b.fHighHz, sr);
				b.energyRaw = E;

				const float absFloorE = 1e-7f;
				const float releaseFast = 12.0f;
				const float aFast = 1.0f - std::exp(-releaseFast * dt);

				if (E < absFloorE) {
					b.energyNorm = 0.0f;
					b.energySmoothed = b.energySmoothed + aFast * (0.0f - b.energySmoothed);
					b.prevTargetU = b.prevTargetU + aFast * (0.0f - b.prevTargetU);

					if (b.agcEnabled) {
						b.agcRunning = b.agcRunning + aFast * (0.0f - b.agcRunning);
					}
					continue;
				}

				float En = E;
				if (b.agcEnabled) {
					const float T = std::max(0.05f, b.agcTimeSec);
					const float alpha = 1.0f - std::exp(-dt / T);
					b.agcRunning = b.agcRunning + alpha * (E - b.agcRunning);

					const float agcMinDenom = 1e-4f;
					const float denom = std::max(std::max(b.agcRunning, b.agcFloor), agcMinDenom);
					En = E / denom;
				}
				b.energyNorm = En;

				float x = En - b.threshold;
				if (x < 0.0f) x = 0.0f;

				const float envDeadzone = 0.003f;
				if (x < envDeadzone) x = 0.0f;

				const float rate = (x > b.energySmoothed) ? b.attack : b.release;
				const float aEnv = 1.0f - std::exp(-rate * dt);
				b.energySmoothed = b.energySmoothed + aEnv * (x - b.energySmoothed);

				const float gx_f = b.pos01.x * float(Nsim - 1);
				const float gy_f = b.pos01.y * float(Nsim - 1);
				const int gx = std::clamp(int(gx_f + 0.5f), 1, Nsim - 2);
				const int gy = std::clamp(int(gy_f + 0.5f), 1, Nsim - 2);

				float desired = b.gain * b.energySmoothed;

				const float maxU = 1.2f;
				desired = maxU * std::tanh(desired / maxU);

				const float maxDeltaU = 0.6f;
				const float delta = std::clamp(desired - b.prevTargetU, -maxDeltaU, maxDeltaU);
				const float targetU = b.prevTargetU + delta;
				b.prevTargetU = targetU;

				if (b.type == AudioBandType::Point) {
					sim.pullPointTargetHeight(gx, gy, dt, targetU, b.radiusCells, b.stiffness, b.damping);
				}
				else {
					const float dirx = std::cos(b.angleRad);
					const float diry = std::sin(b.angleRad);

					const float halfMax = maxHalfLenToBoundsCells(gx_f, gy_f, dirx, diry, Nsim);
					const float halfLen = halfMax * std::clamp(b.length01, 0.0f, 1.0f);
					const float lengthCells = 2.0f * halfLen;

					sim.pullSegmentTargetHeight(
						gx_f, gy_f, dirx, diry, lengthCells,
						dt, targetU,
						b.radiusCells, b.stiffness, b.damping
					);
				}
			}
		}
		else {
			releaseAudioBands(dt);
		}

		wasPlaying = audioPlaying;

		// Sim step
		sim.update(dt);

		// Render
		renderer.updateHeightTexture(sim.packedHV(), sim.N());

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		const glm::vec3 light_dir_ws = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.2f));
		const glm::vec3 cam_pos_ws = mCamera.mWorld.GetTranslation();

		renderer.render(
			water_shader,
			glm::value_ptr(world_to_clip),
			glm::value_ptr(model_to_world),
			glm::value_ptr(normal_to_world),
			glm::value_ptr(light_dir_ws),
			glm::value_ptr(cam_pos_ws),
			SIZE,
			sim.dx(),
			settings.render.vis
		);

		// GUI
		if (show_gui) {
			ui.drawMain(uiState, Nsim, Nr, fps_display);

			if (settings.win.showAudioWindow) {
				audioUi.draw(audioState);
				audioUi.drawSpectrumWindow(audioState);
			}
		}

		if (show_logs) {
			Log::View::Render();
		}

		mWindowManager.RenderImGuiFrame(show_gui);
		glfwSwapBuffers(window);
	}

	glDeleteProgram(water_shader);
}
