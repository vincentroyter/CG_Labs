#pragma once

#include "core/InputHandler.h"
#include "core/FPSCamera.h"
#include "core/WindowManager.hpp"

struct GLFWwindow;

namespace edan35
{
	class ProjectWater {
	public:
		explicit ProjectWater(WindowManager& windowManager);

		~ProjectWater();

		void run();

	private:
		FPSCameraf     mCamera;
		InputHandler   inputHandler;
		WindowManager& mWindowManager;
		GLFWwindow* window;
	};
}
