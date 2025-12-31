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

// ============================================================
// Drivers
// ============================================================
enum class DriverType { Point, Line };
static constexpr float PI = 3.14159265358979323846f;

struct DriverSource
{
	DriverType type = DriverType::Point;

	// Master on/off
	bool enabled = true;

	// Behavior toggles
	bool oscOn = true;     // sine oscillation on/off
	bool spinOn = false;   // spin/orbit on/off

	// Core parameters
	float freqHz = 3.0f;

	// IMPORTANT: in your current implementation this is "target height",
	// not velocity. Keep small or it will blow up.
	float amp = -0.20f;

	// "Thickness" in cells (point footprint / line thickness)
	float width = 5.0f;

	// Center position stored normalized [0,1]
	glm::vec2 pos01 = glm::vec2(0.5f, 0.5f);

	// Spin/orbit parameters
	float spinHz = 0.25f;        // spin/orbit speed
	float spinPhaseRad = 0.0f;   // phase offset (radians)

	// For POINT: orbit radius in normalized units [0..1]
	float orbitRadius01 = 0.03f;

	// LINE-specific
	float angleRad = 0.0f;  // base orientation of the line (radians)
	float length01 = 1.0f;  // 1.0 = spans to bounds along that direction

	std::string name;
};

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
		// Straight above, looking down at origin
		mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 6.0f, 0.001f));
		mCamera.mWorld.SetRotateX(-glm::half_pi<float>());
		};


	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glClearColor(0.02f, 0.02f, 0.03f, 1.0f);

	// ---- Water objects ----
	const float SIZE = 4.0f;

	int Nsim = 128;  // simulation resolution
	int Nr = 512;  // render resolution

	WaterHeightfield sim(Nsim, SIZE);
	WaterMesh        mesh(Nr, SIZE);
	WaterRenderer    renderer;

	renderer.setMesh(mesh.vertexData(), mesh.indices());
	renderer.updateHeightTexture(sim.heights(), sim.N());

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

	// ---- Global UI params ----
	float ui_c = sim.waveSpeed();
	float ui_velDamp = sim.velDamp();
	float ui_maxSlope = sim.maxSlope();

	// Mouse interaction UI (NOTE: used as "per second", multiplied by dt)
	float ui_clickMag = 20.0f;
	int   ui_clickRadius = 4;

	bool ui_nearestHeight = false;

	// Node visualisation
	bool  ui_showNodes = true;
	float ui_nodeEps = 0.01f;
	float ui_nodeStrength = 0.85f;

	// Resolution UI
	int ui_Nsim = Nsim;
	int ui_Nr = Nr;

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

	while (!glfwWindowShouldClose(window)) {
		// ---- Timing ----
		auto now = std::chrono::high_resolution_clock::now();
		auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last);
		float dt = std::chrono::duration<float>(dt_us).count();
		last = now;

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

		glm::mat4 model_to_world(1.0f);
		glm::mat4 world_to_clip = mCamera.GetWorldToClipMatrix();
		glm::mat4 normal_to_world = glm::transpose(glm::inverse(model_to_world));

		// ============================================================
		// 1) Mouse interaction: click/hold/drag splash
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
					// strength per second -> integrate over dt so click/hold feels consistent
					const float mag_dt = ui_clickMag * dt;

					// Click or Hold both apply same amount per frame
					sim.disturb(gx, gy, mag_dt, ui_clickRadius);
				}
			}
		}

		// ============================================================
		// 2) Driver sources (multi)
		//    Uses pullPointTargetHeight / pullSegmentTargetHeight.
		// ============================================================
		for (auto const& d : drivers) {
			if (!d.enabled) continue;

			// target height: static when oscOff, sine when oscOn
			float targetU = d.amp;
			if (d.oscOn) {
				targetU = d.amp * std::sin(2.0f * PI * d.freqHz * simTime);
			}

			// base center
			glm::vec2 p01 = d.pos01;

			// orbit position (applies to BOTH point + line center if enabled)
			if (d.spinOn && d.spinHz > 0.0f && d.orbitRadius01 > 0.0f) {
				float ang = 2.0f * PI * d.spinHz * simTime + d.spinPhaseRad;
				p01 += d.orbitRadius01 * glm::vec2(std::cos(ang), std::sin(ang));
			}

			p01.x = std::clamp(p01.x, 0.0f, 1.0f);
			p01.y = std::clamp(p01.y, 0.0f, 1.0f);

			const float gx = p01.x * float(Nsim - 1);
			const float gy = p01.y * float(Nsim - 1);

			// NOTE: these stiffness/damping values are intentionally moderate.
			// If you still get instability, reduce k first.
			const float k_stiff = 150.0f; // try 50..250
			const float d_damp = 18.0f;  // try 5..30

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
				// line rotation about its center (spin affects angle too)
				float lineAngle = d.angleRad;
				if (d.spinOn && d.spinHz > 0.0f) {
					lineAngle += 2.0f * PI * d.spinHz * simTime + d.spinPhaseRad;
				}

				const float dirx = std::cos(lineAngle);
				const float diry = std::sin(lineAngle);

				// length01=1.0 means "to bounds" in that direction from center
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
		// 3) Sim step + render
		// ============================================================
		sim.update(dt);

		renderer.setHeightFiltering(ui_nearestHeight);
		renderer.updateHeightTexture(sim.heights(), sim.N());

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::vec3 light_dir_ws = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.2f));
		glm::vec3 cam_pos_ws = mCamera.mWorld.GetTranslation();

		renderer.render(
			water_shader,
			glm::value_ptr(world_to_clip),
			glm::value_ptr(model_to_world),
			glm::value_ptr(normal_to_world),
			glm::value_ptr(light_dir_ws),
			glm::value_ptr(cam_pos_ws),
			SIZE,
			sim.dx(),
			ui_showNodes,
			ui_nodeEps,
			ui_nodeStrength
		);

		// ============================================================
		// GUI
		// ============================================================
		if (show_gui) {
			ImGui::Begin("Water Project");

			ImGui::Text("FPS: %.1f", fps_display);
			ImGui::Separator();

			// -----------------------------
			// Help
			// -----------------------------
			if (ImGui::CollapsingHeader("Help", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::BulletText("WASD + mouse = move camera");
				ImGui::BulletText("LMB on water = splash / paint");
				ImGui::BulletText("R = reload shaders");
				ImGui::BulletText("F2/F3 toggle gui/logs");
			}

			ImGui::Separator();
			ImGui::Text("Camera & simulation");

			if (ImGui::Button("Top-down view")) {
				setCameraTopDown();
			}
			ImGui::SameLine();
			if (ImGui::Button("Default view")) {
				setCameraDefault();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset surface")) {
				sim.reset();
			}

			ImGui::Separator();
			if (ImGui::CollapsingHeader("Mesh Resolution (recreate on Apply)")) {
				ImGui::Text("Current: Nsim=%d, Nr=%d", Nsim, Nr);
				ImGui::SliderInt("Nsim (simulation)", &ui_Nsim, 16, 512);
				ImGui::SliderInt("Nr (render mesh)", &ui_Nr, 32, 2048);
				if (ui_Nr < ui_Nsim) ui_Nr = ui_Nsim;

				if (ImGui::Button("Apply Nsim/Nr")) {
					ui_Nsim = std::clamp(ui_Nsim, 16, 512);
					ui_Nr = std::clamp(ui_Nr, 32, 2048);
					if (ui_Nr < ui_Nsim) ui_Nr = ui_Nsim;

					Nsim = ui_Nsim;
					Nr = ui_Nr;

					sim = WaterHeightfield(Nsim, SIZE);
					mesh = WaterMesh(Nr, SIZE);

					renderer.setMesh(mesh.vertexData(), mesh.indices());
					renderer.updateHeightTexture(sim.heights(), sim.N());

					// resync UI from sim
					ui_c = sim.waveSpeed();
					ui_velDamp = sim.velDamp();
					ui_maxSlope = sim.maxSlope();
				}

			}
		}



			// -----------------------------
			// Global simulation
			// -----------------------------
			if (ImGui::CollapsingHeader("Global simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Wave / damping");
				ImGui::SliderFloat("Wave speed c", &ui_c, 0.2f, 3.0f, "%.2f");
				ImGui::SliderFloat("Damping gamma (1/s)", &ui_velDamp, 0.0f, 6.0f, "%.2f");
				ImGui::SliderFloat("Clamp maxSlope", &ui_maxSlope, 0.05f, 1.5f, "%.2f");

				ImGui::Separator();
				ImGui::Text("Rendering");
				ImGui::Checkbox("Height sampling: NEAREST", &ui_nearestHeight);

				ImGui::Separator();
				ImGui::Text("Node/zero-crossing visualization");
				ImGui::Checkbox("Show node lines", &ui_showNodes);
				ImGui::SliderFloat("Node threshold (eps)", &ui_nodeEps, 0.0001f, 0.05f, "%.4f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Node strength", &ui_nodeStrength, 0.0f, 1.0f, "%.2f");


			// -----------------------------
			// Mouse interaction
			// -----------------------------
			if (ImGui::CollapsingHeader("Mouse interaction", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Click = splash. Hold+drag = continuous splash.");

				ImGui::SliderFloat("Splash strength (per sec)", &ui_clickMag, -50.0f, 50.0f, "%.3f");
				ImGui::SliderInt("Splash radius (cells)", &ui_clickRadius, 1, 25);

				if (ImGui::Button("Reset splash defaults")) {
					ui_clickMag = 20.0f;
					ui_clickRadius = 4;
				}
			}

			// -----------------------------
			// Driver sources
			// -----------------------------
			if (ImGui::CollapsingHeader("Driver sources")) {

				if (ImGui::Button("Add point")) {
					DriverSource d;
					d.type = DriverType::Point;
					d.pos01 = glm::vec2(0.5f, 0.5f);
					d.name = "Point " + std::to_string(driverCounter++);
					drivers.push_back(d);
					selectedDriver = int(drivers.size()) - 1;
				}
				ImGui::SameLine();
				if (ImGui::Button("Add line")) {
					DriverSource d;
					d.type = DriverType::Line;
					d.pos01 = glm::vec2(0.5f, 0.5f);
					d.angleRad = 0.0f;
					d.length01 = 1.0f;
					d.name = "Line " + std::to_string(driverCounter++);
					drivers.push_back(d);
					selectedDriver = int(drivers.size()) - 1;
				}
				ImGui::SameLine();
				if (ImGui::Button("All ON")) {
					for (auto& d : drivers) d.enabled = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("All OFF")) {
					for (auto& d : drivers) d.enabled = false;
				}

				ImGui::Separator();

				if (ImGui::BeginListBox("##drivers", ImVec2(-FLT_MIN, 140.0f))) {
					for (int i = 0; i < (int)drivers.size(); ++i) {
						bool isSelected = (selectedDriver == i);

						std::string label = (drivers[i].enabled ? "[on] " : "[off] ");
						label += drivers[i].name;
						label += (drivers[i].type == DriverType::Point) ? " (Point)" : " (Line)";

						if (ImGui::Selectable(label.c_str(), isSelected)) {
							selectedDriver = i;
						}
						if (isSelected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndListBox();
				}

				if (selectedDriver >= 0 && selectedDriver < (int)drivers.size()) {
					DriverSource& d = drivers[selectedDriver];

					ImGui::Separator();
					ImGui::Text("Selected: %s", d.name.c_str());

					ImGui::Checkbox("Enabled", &d.enabled);

					ImGui::Checkbox("Oscillation (sine)", &d.oscOn);
					ImGui::SameLine();
					ImGui::Checkbox("Spin/orbit", &d.spinOn);

					ImGui::Separator();
					ImGui::Text("Core");
					ImGui::SliderFloat("Frequency (Hz)", &d.freqHz, 0.1f, 20.0f, "%.2f");
					ImGui::SliderFloat("Amplitude (height)", &d.amp, -1.0f, 1.0f, "%.3f");
					ImGui::SliderFloat2("Center (u,v)", &d.pos01.x, 0.0f, 1.0f, "%.3f");
					ImGui::SliderFloat("Width (cells)", &d.width, 1.0f, 20.0f, "%.1f");

					ImGui::Separator();
					ImGui::Text("Spin/orbit");
					ImGui::BeginDisabled(!d.spinOn);
					ImGui::SliderFloat("Spin frequency (Hz)", &d.spinHz, 0.0f, 5.0f, "%.2f");
					ImGui::SliderAngle("Spin phase", &d.spinPhaseRad, -180.0f, 180.0f);

					if (d.type == DriverType::Point) {
						ImGui::SliderFloat("Orbit radius", &d.orbitRadius01, 0.0f, 0.35f, "%.3f");
					}
					ImGui::EndDisabled();

					if (d.type == DriverType::Line) {
						ImGui::Separator();
						ImGui::Text("Line shape");
						ImGui::SliderFloat("Length", &d.length01, 0.05f, 1.0f, "%.2f");
						ImGui::SliderAngle("Base orientation", &d.angleRad, -180.0f, 180.0f);
					}

					ImGui::Separator();
					if (ImGui::Button("Delete source")) {
						drivers.erase(drivers.begin() + selectedDriver);
						if (drivers.empty()) selectedDriver = -1;
						else selectedDriver = std::clamp(selectedDriver, 0, (int)drivers.size() - 1);
					}
				}
				else {
					ImGui::TextDisabled("No driver selected.");
				}
			}

			// -----------------------------
			// Reset / safety
			// -----------------------------
			if (ImGui::CollapsingHeader("Reset / safety")) {
				if (ImGui::Button("Reset global defaults")) {
					ui_c = 1.2f;
					ui_velDamp = 1.0f;
					ui_maxSlope = 0.6f;
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear all drivers")) {
					drivers.clear();
					selectedDriver = -1;
				}
			}

			ImGui::End();
		}

		if (show_logs)
			Log::View::Render();

		mWindowManager.RenderImGuiFrame(show_gui);
		glfwSwapBuffers(window);
	}

	glDeleteProgram(water_shader);
}
