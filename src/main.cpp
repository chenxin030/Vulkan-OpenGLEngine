#include <GLFW/glfw3.h>
#include <iostream>
#include "vulkanTools.h"
#include <stdexcept>
#include <cstdlib>

// helper
void ErrorCallback(int error, const char* description)
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
    glfwSetErrorCallback(ErrorCallback);
    if (!glfwInit()) {
        std::cerr << "ERROR: GLFW init\n";
        return EXIT_FAILURE;
    }

    GLFWwindow* window = nullptr;
    int exitCode = EXIT_SUCCESS;

    try {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        window = glfwCreateWindow(1600, 900, "Engine", NULL, NULL);
        if (!window)
            throw std::runtime_error("ERROR: GLFW window create");
        glfwSetKeyCallback(window, KeyCallback);

        VulkanTools vk(window);
        vk.init();

        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
        }

        vk.cleanup();
    }
    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        exitCode = EXIT_FAILURE;
    }

    if (window != nullptr) { glfwDestroyWindow(window); }
    glfwTerminate();
    return exitCode;
}
