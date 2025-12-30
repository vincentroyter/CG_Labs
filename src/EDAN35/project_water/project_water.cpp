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

#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <string>


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


void edan35::ProjectWater::run()
{
	// Camera setup
	mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 2.0f, 4.0f));
	mCamera.mWorld.SetRotateX(-0.5f);
	mCamera.mMouseSensitivity = glm::vec2(0.003f);
	mCamera.mMovementSpeed = glm::vec3(3.0f);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glClearColor(0.02f, 0.02f, 0.03f, 1.0f);

	// ---- Water objects ----
	const float SIZE = 4.0f;

	// Runtime-changeable resolutions
	int Nsim = 128;   // simulation resolution
	int Nr = 512;   // render resolution

	// Create initial objects
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

	// ---- UI-controlled parameters ----
	float ui_c = sim.waveSpeed();
	float ui_velDamp = sim.velDamp();
	float ui_maxSlope = sim.maxSlope();

	float ui_clickMag = -10.0f;
	int   ui_clickRadius = 4;
	bool ui_nearestHeight = false;


	// Resolutions (requested + applied via button)
	int ui_Nsim = Nsim;
	int ui_Nr = Nr;

	// FPS tracking
	float fps_display = 0.0f;

	double fps_accum_time = 0.0;
	int    fps_accum_frames = 0;

	auto last = std::chrono::high_resolution_clock::now();
	bool shader_reload_failed = false;

	while (!glfwWindowShouldClose(window)) {
		// ---- Timing ----
		auto now = std::chrono::high_resolution_clock::now();
		auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last);
		float dt = std::chrono::duration<float>(dt_us).count();
		last = now;

		// --- FPS ---
		fps_accum_time += double(dt);
		fps_accum_frames += 1;

		// Update the displayed FPS 1 times per second
		if (fps_accum_time >= 1) {
			fps_display = float(double(fps_accum_frames) / fps_accum_time);

			fps_accum_time = 0.0;
			fps_accum_frames = 0;
		}
		// ---- Input/camera ----
		auto& io = ImGui::GetIO();
		inputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);

		glfwPollEvents();
		inputHandler.Advance();
		mCamera.Update(dt_us, inputHandler);

		if (inputHandler.GetKeycodeState(GLFW_KEY_R) & JUST_PRESSED) {
			shader_reload_failed = !program_manager.ReloadAllPrograms();
			if (shader_reload_failed)
				tinyfd_notifyPopup("Shader Program Reload Error",
					"An error occurred while reloading shader programs; see the logs for details.\n"
					"Rendering is suspended until the issue is solved. Once fixed, just reload the shaders again.",
					"error");
		}
		if (inputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED) show_gui = !show_gui;
		if (inputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED) show_logs = !show_logs;
		if (inputHandler.GetKeycodeState(GLFW_KEY_F11) & JUST_RELEASED) mWindowManager.ToggleFullscreenStatusForWindow(window);

		int framebuffer_width, framebuffer_height;
		glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
		glViewport(0, 0, framebuffer_width, framebuffer_height);

		mWindowManager.NewImGuiFrame();

		// Apply live simulation params
		sim.setWaveSpeed(ui_c);
		sim.setVelDamp(ui_velDamp);
		sim.setMaxSlope(ui_maxSlope);


		// ---- Splash point ----
		if (!io.WantCaptureMouse && (inputHandler.GetMouseState(GLFW_MOUSE_BUTTON_LEFT) & JUST_PRESSED)) {
			double mx, my;
			glfwGetCursorPos(window, &mx, &my);

			int w = framebuffer_width;
			int h = framebuffer_height;

			int gx = 0, gy = 0;
			glm::mat4 model_to_world(1.0f);
			glm::mat4 world_to_clip = mCamera.GetWorldToClipMatrix();

			if (pickWaterGrid(mx, my, w, h, world_to_clip, model_to_world, SIZE, Nsim, gx, gy)) {
				sim.disturb(gx, gy, ui_clickMag, ui_clickRadius);
			}

		}

		// ---- Sim step ----
		sim.update(dt);

		// ---- Mesh update ----
		renderer.updateHeightTexture(sim.heights(), sim.N());

		// ---- Render ----
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::mat4 model_to_world(1.0f);
		glm::mat4 world_to_clip = mCamera.GetWorldToClipMatrix();
		glm::mat4 normal_to_world = glm::transpose(glm::inverse(model_to_world));

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
			sim.dx()
		);


		// ---- GUI ----
		if (show_gui) {
			ImGui::Begin("Water Project");

			ImGui::Text("FPS: %.1f", fps_display);
			ImGui::Separator();

			ImGui::Text("WASD + mouse to move");
			ImGui::Text("LMB = splash");
			ImGui::Text("R = reload shaders, F2/F3 toggle gui/logs");
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Simulation parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Wave speed c", &ui_c, 0.2f, 5.0f, "%.2f");
				ImGui::SliderFloat("Velocity damping", &ui_velDamp, 0.90f, 0.9999f, "%.4f");
				ImGui::SliderFloat("Clamp maxSlope", &ui_maxSlope, 0.05f, 2.0f, "%.2f");

				ImGui::Separator();
				ImGui::SliderFloat("Disturb magnitude", &ui_clickMag, -50.0f, 50.0f, "%.1f");
				ImGui::SliderInt("Disturb radius", &ui_clickRadius, 1, 30);

				if (ImGui::Button("Reset sim defaults")) {
					ui_c = 1.2f;
					ui_velDamp = 0.995f;
					ui_maxSlope = 0.6f;
					ui_clickMag = -10.0f;
					ui_clickRadius = 4;
					ui_nearestHeight = false;
				}
			}

			if (ImGui::CollapsingHeader("WaterMesh Resolution (recreate on Apply)", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Current: Nsim=%d, Nr=%d", Nsim, Nr);
				ImGui::SliderInt("Nsim (simulation)", &ui_Nsim, 8, 512);
				ImGui::SliderInt("Nr (render mesh)", &ui_Nr, 8, 2048);

				ImGui::Checkbox("Height sampling: NEAREST", &ui_nearestHeight);
				renderer.setHeightFiltering(ui_nearestHeight);

				// Keep sensible:
				if (ui_Nr < ui_Nsim) ui_Nr = ui_Nsim;


				if (ImGui::Button("Apply Nsim/Nr")) {
					// Clamp to safe values
					ui_Nsim = std::clamp(ui_Nsim, 8, 512);
					ui_Nr = std::clamp(ui_Nr, 8, 2048);

					if (ui_Nr < ui_Nsim) ui_Nr = ui_Nsim;

					Nsim = ui_Nsim;
					Nr = ui_Nr;

					// Recreate objects
					sim = WaterHeightfield(Nsim, SIZE);
					mesh = WaterMesh(Nr, SIZE);

					renderer.setMesh(mesh.vertexData(), mesh.indices());
					renderer.updateHeightTexture(sim.heights(), sim.N());

					// Re-sync UI parameters from the new sim
					ui_c = sim.waveSpeed();
					ui_velDamp = sim.velDamp();
					ui_maxSlope = sim.maxSlope();
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
