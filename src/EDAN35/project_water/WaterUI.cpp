#include "WaterUI.hpp"
#include "DriverSource.hpp" // declare this separately

#include "WaterHeightfield.hpp"
#include "WaterMesh.hpp"
#include "WaterRenderer.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <string>
#include <algorithm>
#include <format> // for future use

// ---- Reuse the slider with input widgets ----
static bool SliderFloatWithInput(const char* label, float* value, float min, float max, const char* format = "%.2f")
{
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SameLine();

	ImGui::PushItemWidth(130.0f);
	bool changed1 = ImGui::SliderFloat("##slider", value, min, max, format);
	ImGui::SameLine();

	ImGui::PushItemWidth(60.0f);
	bool changed2 = ImGui::InputFloat("##input", value, 0.0f, 0.0f, format, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopItemWidth();

	*value = std::clamp(*value, min, max);
	ImGui::PopID();
	return changed1 || changed2;
}

static bool SliderIntWithInput(const char* label, int* value, int min, int max)
{
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SameLine();

	ImGui::PushItemWidth(130.0f);
	bool changed1 = ImGui::SliderInt("##slider", value, min, max);
	ImGui::SameLine();

	ImGui::PushItemWidth(60.0f);
	int step = 1;
	bool changed2 = ImGui::InputInt("##input", value, step, step, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopItemWidth();

	*value = std::clamp(*value, min, max);
	ImGui::PopID();
	return changed1 || changed2;
}

void WaterUI::draw(
	WaterUIState& s,
	int currentNsim,
	int currentNr,
	float fps)
{
	if (!ImGui::Begin("Water Project")) {
		ImGui::End();
		return;
	}

	ImGui::Text("FPS: %.1f", fps);
	ImGui::Separator();

	// ------------------------------------------------------------
	// Camera & Simulation Controls
	// ------------------------------------------------------------
	ImGui::Text("Camera & simulation");
	if (ImGui::Button("Top-down view")) s.setCameraTopDown();
	ImGui::SameLine();
	if (ImGui::Button("Default view")) s.setCameraDefault();
	ImGui::SameLine();
	if (ImGui::Button("Reset surface")) s.resetSurface();

	// ------------------------------------------------------------
	// Resolution Controls
	// ------------------------------------------------------------
	ImGui::Separator();
	if (ImGui::CollapsingHeader("Mesh Resolution (recreate on Apply)")) {
		ImGui::Text("Current: Nsim=%d, Nr=%d", currentNsim, currentNr);

		bool changed = false;
		changed |= SliderIntWithInput("Nsim (simulation)", s.Nsim, 16, 512);
		changed |= SliderIntWithInput("Nr (render mesh)", s.Nr, 32, 2048);

		// Show pending values
		ImGui::Text("Pending: Nsim=%d, Nr=%d", *s.Nsim, *s.Nr);

		// Enable Apply only if different from current
		bool canApply = (*s.Nsim != currentNsim) || (*s.Nr != currentNr);
		ImGui::BeginDisabled(!canApply || !s.applyResolution);
		if (ImGui::Button("Apply")) {
			s.applyResolution(*s.Nsim, *s.Nr);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Revert")) {
			*s.Nsim = currentNsim;
			*s.Nr = currentNr;
		}
		ImGui::EndDisabled();

		if (changed && !canApply) {
			// no-op; just keeps UI responsive
		}
	}


	// ------------------------------------------------------------
	// Global Simulation Settings
	// ------------------------------------------------------------
	if (ImGui::CollapsingHeader("Global simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
		SliderFloatWithInput("Wave speed c", s.waveSpeed, 0.2f, 3.0f, "%.2f");
		SliderFloatWithInput("Damping gamma (1/s)", s.velDamp, 0.0f, 6.0f, "%.2f");
		SliderFloatWithInput("Clamp maxSlope", s.maxSlope, 0.05f, 1.5f, "%.2f");

		ImGui::Separator();
		ImGui::Text("Rendering");
		ImGui::Checkbox("Height sampling: NEAREST", s.nearestHeight);

		ImGui::Separator();
		ImGui::Text("Node/zero-crossing visualization");
		ImGui::Checkbox("Show node lines", s.showNodes);
		SliderFloatWithInput("Node threshold (eps)", s.nodeEps, 0.0001f, 0.05f, "%.4f");
		SliderFloatWithInput("Node strength", s.nodeStrength, 0.0f, 1.0f, "%.2f");

		ImGui::Separator();
		ImGui::Text("Visual effects");

		// -------------------------
		// Height coloring + theme
		// -------------------------
		if (s.useHeightColoring) {
			ImGui::Checkbox("Height coloring", s.useHeightColoring);
		}

		if (s.colorTheme) {
			ImGui::BeginDisabled(!(s.useHeightColoring && *s.useHeightColoring));
			const char* themes[] = { "Ocean", "Thermal", "Psychedelic" };
			ImGui::Combo("Theme", s.colorTheme, themes, IM_ARRAYSIZE(themes));
			ImGui::EndDisabled();
		}

		// -------------------------
		// Specular controls
		// -------------------------
		ImGui::Separator();
		ImGui::Text("Specular");

		if (s.enableSpecular) {
			ImGui::Checkbox("Enable specular", s.enableSpecular);
		}
		if (s.specularStrength) {
			ImGui::BeginDisabled(!(s.enableSpecular && *s.enableSpecular));
			SliderFloatWithInput("Specular strength", s.specularStrength, 0.0f, 1.5f, "%.3f");
			ImGui::EndDisabled();
		}
		if (s.specularPower) {
			ImGui::BeginDisabled(!(s.enableSpecular && *s.enableSpecular));
			SliderFloatWithInput("Specular power", s.specularPower, 1.0f, 256.0f, "%.1f");
			ImGui::EndDisabled();
		}

		// -------------------------
		// Foam (prepared; shader can implement later)
		// -------------------------
		ImGui::Separator();
		ImGui::Text("Foam (prepared)");

		if (s.enableFoam) {
			ImGui::Checkbox("Enable foam", s.enableFoam);
		}
		if (s.foamThreshold) {
			ImGui::BeginDisabled(!(s.enableFoam && *s.enableFoam));
			SliderFloatWithInput("Foam threshold", s.foamThreshold, 0.0f, 2.0f, "%.3f");
			ImGui::EndDisabled();
		}

		// -------------------------
		// Velocity visualization (prepared)
		// -------------------------
		ImGui::Separator();
		ImGui::Text("Velocity");

		if (s.velocityColoring) {
			ImGui::Checkbox("Velocity coloring", s.velocityColoring);
		}



		ImGui::Separator();
		ImGui::Text("Boundary conditions");
		ImGui::Checkbox("Use open boundaries", s.openBoundary);



	}

	// ------------------------------------------------------------
	// Mouse Interaction
	// ------------------------------------------------------------
	if (ImGui::CollapsingHeader("Mouse interaction")) {
		SliderFloatWithInput("Click/press strength", s.clickMag, -50.0f, 50.0f, "%.2f");
		SliderIntWithInput("Splash radius (cells)", s.clickRadius, 1, 25);
	}

	// ------------------------------------------------------------
	// Driver Sources
	// ------------------------------------------------------------
	if (ImGui::CollapsingHeader("Driver sources")) {
		if (ImGui::Button("Add point")) {
			DriverSource d;
			d.type = DriverType::Point;
			d.name = "Point " + std::to_string((*s.driverCounter)++);
			s.drivers->push_back(d);
			*s.selectedDriver = int(s.drivers->size()) - 1;
		}
		ImGui::SameLine();
		if (ImGui::Button("Add line")) {
			DriverSource d;
			d.type = DriverType::Line;
			d.name = "Line " + std::to_string((*s.driverCounter)++);
			s.drivers->push_back(d);
			*s.selectedDriver = int(s.drivers->size()) - 1;
		}

		ImGui::Separator();

		// List all drivers
		if (ImGui::BeginListBox("##drivers", ImVec2(-FLT_MIN, 140.0f))) {
			for (int i = 0; i < (int)s.drivers->size(); ++i) {
				std::string label = (*s.drivers)[i].name;
				if (ImGui::Selectable(label.c_str(), *s.selectedDriver == i)) {
					*s.selectedDriver = i;
				}
			}
			ImGui::EndListBox();
		}

		// ----------------------------
		// Driver editing
		// ----------------------------
		if (*s.selectedDriver >= 0 && *s.selectedDriver < (int)s.drivers->size()) {
			auto& d = (*s.drivers)[*s.selectedDriver];

			ImGui::Separator();
			ImGui::Text("Selected: %s", d.name.c_str());

			ImGui::Checkbox("Enabled", &d.enabled);

			ImGui::Checkbox("Oscillation (sine)", &d.oscOn);
			ImGui::SameLine();
			ImGui::Checkbox("Spin/orbit", &d.spinOn);

			// ----------------------------
			// Position, Width
			// ----------------------------
			ImGui::Separator();
			SliderFloatWithInput("Source Strength", &d.amp, -1.0f, 1.0f, "%.2f");
			SliderFloatWithInput("Width", &d.width, 1.0f, 20.0f, "%.1f");
			SliderFloatWithInput("Center U", &d.pos01.x, 0.0f, 1.0f, "%.3f");
			SliderFloatWithInput("Center V", &d.pos01.y, 0.0f, 1.0f, "%.3f");

			// ----------------------------
			// Oscillation Settings
			// ----------------------------
			ImGui::Separator();
			ImGui::Text("Oscillation");

			ImGui::BeginDisabled(!d.oscOn);
			SliderFloatWithInput("Frequency (Hz)", &d.freqHz, 0.1f, 20.0f, "%.2f");
			ImGui::SliderAngle("Phase (deg)", &d.oscPhaseRad, -180.0f, 180.0f);
			ImGui::EndDisabled();

			// ----------------------------
			// Spin/Orbit Settings
			// ----------------------------
			ImGui::Separator();
			ImGui::Text("Spin/orbit");
			ImGui::BeginDisabled(!d.spinOn);
			SliderFloatWithInput("Spin frequency (Hz)", &d.spinHz, 0.0f, 5.0f, "%.2f");
			ImGui::SliderAngle("Spin phase", &d.spinPhaseRad, -180.0f, 180.0f);

			if (d.type == DriverType::Point) {
				SliderFloatWithInput("Orbit radius", &d.orbitRadius01, 0.0f, 0.35f, "%.3f");
			}
			ImGui::EndDisabled();

			// ----------------------------
			// Line-specific shape
			// ----------------------------
			if (d.type == DriverType::Line) {
				ImGui::Separator();
				ImGui::Text("Line shape");
				SliderFloatWithInput("Length", &d.length01, 0.05f, 1.0f, "%.2f");
				ImGui::SliderAngle("Base orientation", &d.angleRad, -180.0f, 180.0f);
			}

			// ----------------------------
			// Delete Button
			// ----------------------------
			ImGui::Separator();
			if (ImGui::Button("Delete source")) {
				s.drivers->erase(s.drivers->begin() + *s.selectedDriver);
				if (s.drivers->empty())
					*s.selectedDriver = -1;
				else
					*s.selectedDriver = std::clamp(*s.selectedDriver, 0, (int)s.drivers->size() - 1);
			}
		}
		else {
			ImGui::TextDisabled("No driver selected.");
		}
	}

	ImGui::End();
}

