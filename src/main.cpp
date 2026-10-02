#include "platform/GlfwInput.h"
#include "rhi/Renderer.h"
#include "rhi/RendererFactory.h"
#include "scene/CameraController.h"
#include "scene/Scene.h"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <cstdlib>

// helper
void GlfwErrorCallback(int error, const char* description)
{
    std::cerr << "[glfw] (" << error << ") " << description << std::endl;
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
int main()
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "ERROR: GLFW init\n";
        return EXIT_FAILURE;
    }

    GLFWwindow* window = nullptr;
    int exitCode = EXIT_SUCCESS;

    try {
        std::unique_ptr<Renderer> renderer = CreateRenderer(GraphicAPIType::VULKAN);
        renderer->ConfigureWindowHints();
        window = glfwCreateWindow(1600, 900, "Engine", NULL, NULL);
        if (!window)
            throw std::runtime_error("ERROR: GLFW window create");
        glfwSetKeyCallback(window, KeyCallback);

        Scene scene = BuildDemoScene();
        GlfwInput input(window);
        const CameraController cameraController;

        renderer->Init(window);

        double previousTime = glfwGetTime();

        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            const double now = glfwGetTime();
            const float deltaSeconds = static_cast<float>(now - previousTime);
            previousTime = now;

            const InputState inputState = input.Sample();
            cameraController.Update(scene.camera, inputState, deltaSeconds);
            scene.Update(deltaSeconds);

            renderer->DrawFrame(scene);
        }
        renderer->WaitIdle();
        renderer->Cleanup();
    }
    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        exitCode = EXIT_FAILURE;
    }

    if (window != nullptr) { glfwDestroyWindow(window); }
    glfwTerminate();
    return exitCode;
}
