#include "WaterUI.hpp"

#include <imgui.h>
#include <algorithm>
#include <string>

static bool SliderFloatWithInput(const char* label, float* value, float min, float max, const char* format = "%.2f")
{
	if (!value) return false;

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
	if (!value) return false;

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

void WaterUI::drawMain(WaterUIState& s, int currentNsim, int currentNr, float fps)
{
	if (!ImGui::Begin("Water Project")) {
		ImGui::End();
		return;
	}

	if (!s.settings) {
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error: uiState.settings is null.");
		ImGui::End();
		return;
	}
	auto& set = *s.settings;

	// Presets row
	if (s.savePreset && ImGui::Button("Save Preset")) s.savePreset();
	ImGui::SameLine();
	if (s.loadPreset && ImGui::Button("Load Preset")) s.loadPreset();
	ImGui::Separator();

	ImGui::Text("FPS: %.1f", fps);
	ImGui::Separator();

	ImGui::TextUnformatted("Camera and simulation");
	if (ImGui::Button("Top-down view") && s.setCameraTopDown) s.setCameraTopDown();
	ImGui::SameLine();
	if (ImGui::Button("Default view") && s.setCameraDefault) s.setCameraDefault();
	ImGui::SameLine();
	if (ImGui::Button("Reset surface") && s.resetSurface) s.resetSurface();

	ImGui::Separator();
	ImGui::TextUnformatted("Windows");
	ImGui::Checkbox("Audio window", &set.win.showAudioWindow);

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Mesh Resolution")) {
		ImGui::Text("Current: Nsim=%d, Nr=%d", currentNsim, currentNr);

		SliderIntWithInput("Simulation res", &set.res.Nsim, 16, 512);
		SliderIntWithInput("Render res", &set.res.Nr, 32, 2048);

		const bool canApply = (set.res.Nsim != currentNsim) || (set.res.Nr != currentNr);

		ImGui::BeginDisabled(!canApply || !s.applyResolution);
		if (ImGui::Button("Apply") && s.applyResolution) {
			s.applyResolution(set.res.Nsim, set.res.Nr);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Revert")) {
			set.res.Nsim = currentNsim;
			set.res.Nr = currentNr;
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextUnformatted("Rendering");
		ImGui::Checkbox("Height filtering: Nearest Neighbour", &set.render.nearestHeight);
	}

	if (ImGui::CollapsingHeader("Global Simulation Settings")) {
		SliderFloatWithInput("Wave speed c", &set.sim.c, 0.2f, 3.0f, "%.2f");
		SliderFloatWithInput("Damping gamma (1/s)", &set.sim.velDamp, 0.0f, 6.0f, "%.2f");
		SliderFloatWithInput("Clamp maxSlope", &set.sim.maxSlope, 0.05f, 3.5f, "%.2f");
		SliderFloatWithInput("Viscosity nu", &set.sim.viscosity, 0.0f, 0.06f, "%.4f");
		ImGui::Checkbox("Lock water level", &set.sim.lockWaterLevel);
		ImGui::Checkbox("Use open boundaries", &set.sim.openBoundary);
	}

	if (ImGui::CollapsingHeader("Mouse interaction")) {
		SliderFloatWithInput("Magnitude", &set.mouse.clickMag, -50.0f, 50.0f, "%.2f");
		SliderIntWithInput("Radius", &set.mouse.clickRadius, 1, 25);
	}

	if (ImGui::CollapsingHeader("Visual Settings")) {
		if (ImGui::CollapsingHeader("Node Lines")) {
			ImGui::Checkbox("Show node lines", &set.render.vis.showNodes);
			SliderFloatWithInput("Node threshold", &set.render.vis.nodeEps, 0.0001f, 0.05f, "%.4f");
			SliderFloatWithInput("Node strength", &set.render.vis.nodeStrength, 0.0f, 1.0f, "%.2f");
		}

		if (ImGui::CollapsingHeader("Height Colors")) {
			ImGui::Checkbox("Height coloring", &set.render.vis.useHeightColoring);
			{
				const bool enableTheme = set.render.vis.useHeightColoring;
				ImGui::BeginDisabled(!enableTheme);
				const char* themes[] = { "Ocean", "Thermal", "Psychedelic" };
				ImGui::Combo("Theme", &set.render.vis.colorTheme, themes, IM_ARRAYSIZE(themes));
				ImGui::EndDisabled();
			}
		}

		if (ImGui::CollapsingHeader("Specular")) {
			ImGui::Checkbox("Enable specular", &set.render.vis.enableSpecular);
			{
				const bool specOn = set.render.vis.enableSpecular;
				ImGui::BeginDisabled(!specOn);
				SliderFloatWithInput("Specular strength", &set.render.vis.specularStrength, 0.0f, 1.5f, "%.3f");
				SliderFloatWithInput("Specular power", &set.render.vis.specularPower, 1.0f, 256.0f, "%.1f");
				ImGui::EndDisabled();
			}
		}

		if (ImGui::CollapsingHeader("Derivative Colors")) {

			if (ImGui::CollapsingHeader("Velocity magnitude")) {
				ImGui::PushID("vel");
				ImGui::Checkbox("Enable velocity tint", &set.render.vis.velocityEnabled);

				const bool on = set.render.vis.velocityEnabled;
				ImGui::BeginDisabled(!on);

				SliderFloatWithInput("Sensitivity", &set.render.vis.velocityScale, 0.0f, 10.0f, "%.2f");
				SliderFloatWithInput("Threshold", &set.render.vis.velocityThreshold, 0.0f, 5.0f, "%.2f");
				SliderFloatWithInput("Strength", &set.render.vis.velocityStrength, 0.0f, 2.0f, "%.2f");

				ImGui::TextUnformatted("Color");
				ImGui::ColorEdit3("##velcol", set.render.vis.velocityColor);

				ImGui::EndDisabled();
				ImGui::PopID();
			}

			if (ImGui::CollapsingHeader("Slope magnitude (1st derivative)")) {
				ImGui::PushID("slope");
				ImGui::Checkbox("Enable slope tint", &set.render.vis.slopeEnabled);

				const bool on = set.render.vis.slopeEnabled;
				ImGui::BeginDisabled(!on);

				SliderFloatWithInput("Sensitivity", &set.render.vis.slopeScale, 0.0f, 10.0f, "%.2f");
				SliderFloatWithInput("Threshold", &set.render.vis.slopeThreshold, 0.0f, 2.0f, "%.2f");
				SliderFloatWithInput("Strength", &set.render.vis.slopeStrength, 0.0f, 2.0f, "%.2f");

				ImGui::TextUnformatted("Color");
				ImGui::ColorEdit3("##slopecol", set.render.vis.slopeColor);

				ImGui::EndDisabled();
				ImGui::PopID();
			}

			if (ImGui::CollapsingHeader("Curvature magnitude (2nd derivative)")) {
				ImGui::PushID("curve");
				ImGui::Checkbox("Enable curvature tint", &set.render.vis.curvatureEnabled);

				const bool on = set.render.vis.curvatureEnabled;
				ImGui::BeginDisabled(!on);

				SliderFloatWithInput("Sensitivity", &set.render.vis.curvatureScale, 0.0f, 50.0f, "%.2f");
				SliderFloatWithInput("Threshold", &set.render.vis.curvatureThreshold, 0.0f, 5.0f, "%.2f");
				SliderFloatWithInput("Strength", &set.render.vis.curvatureStrength, 0.0f, 2.0f, "%.2f");

				ImGui::TextUnformatted("Color");
				ImGui::ColorEdit3("##curvcol", set.render.vis.curvatureColor);

				ImGui::EndDisabled();
				ImGui::PopID();
			}
		}
	}

	ImGui::End();
}
