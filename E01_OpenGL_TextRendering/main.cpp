#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "TextRendering.h"


GLFWwindow* window;

bool init_openGL();
double calc_deltaTime();

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods); // Name der Funktion frei wählbar, Parameter sind aber vorgegeben!


int main()
{
	if (!init_openGL())
		return 1;
	TextRendering text;
	if (!text.Init("fonts/NotoSans-Regular.ttf", 32, 640, 480))
	{
		std::cerr << "Text rendering initialization failed.\n";
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}
	
	while (!glfwWindowShouldClose(window))
	{
		double delta_time = calc_deltaTime();

		glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);	// Alternative Buffer: Depth_Buffer, Stencil_Buffer

		text.DrawText("OpenGL + FreeType", 24, 20);
		text.DrawText("ASCII text in yellow", 24, 90, 0.75f, 1.0f, 0.85f, 0.2f);
		text.DrawText("Large text", 24, 155, 1.5f, 0.4f, 0.85f, 1.0f);
		text.DrawRectangle(24, 290, 390, 85, 0.08f, 0.12f, 0.18f);
		text.DrawText("GUI: Hello, world!", 42, 308, 1.0f, 0.5f, 1.0f, 0.6f);

		glfwSwapBuffers(window);

		glfwPollEvents();
	}

	text.Shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}

bool init_openGL()
{
	if (!glfwInit())
	{
		std::cerr << "GLFW initialization failed.\n";
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// GameInstitute: Beispiel für Fullscreen enthalten
	window = glfwCreateWindow(640, 480, "OpenGL 4.6", NULL, NULL);
	if (!window)
	{
		const char* description = nullptr;
		glfwGetError(&description);
		std::cerr << "Window creation failed: " << (description ? description : "unknown error") << '\n';
		glfwTerminate();
		return false;
	}

	glfwSetKeyCallback(window, key_callback);	// 2 Argumente: aktives Window + aufzurufende Funktion (Name kann frei gewählt werden)

	glfwMakeContextCurrent(window);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cerr << "GLAD initialization failed.\n";
		glfwDestroyWindow(window);
		glfwTerminate();
		return false;
	}

	GLint profile = 0;
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
	std::cout << "GL_VERSION: " << glGetString(GL_VERSION) << '\n'
		<< "GL_VENDOR: " << glGetString(GL_VENDOR) << '\n'
		<< "GL_RENDERER: " << glGetString(GL_RENDERER) << '\n'
		<< "Context profile: "
		<< ((profile & GL_CONTEXT_CORE_PROFILE_BIT) ? "Core" :
			(profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) ? "Compatibility" : "Unknown")
		<< " (mask: " << profile << ')' << std::endl;

	glfwSwapInterval(1);
	return true;
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE); // GameInstitute: Parameter 2: GL_True
}

double calc_deltaTime() // GameInstitute: void showFP(GLFW* window)
{
	static double lastFrame = 0.0f;
	double currentFrame = glfwGetTime();
	double deltaTime = currentFrame - lastFrame;

	lastFrame = currentFrame;

	return deltaTime;
}
