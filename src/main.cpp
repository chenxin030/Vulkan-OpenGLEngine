#include "editor/Editor.h"
#include "platform/GlfwInput.h"
#include "rhi/Renderer.h"
#include "rhi/RendererFactory.h"
#include "scene/CameraController.h"
#include "scene/RenderSnapshot.h"
#include "scene/Scene.h"
#include "scene/SceneUpdater.h"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <cstdlib>
#include <optional>

// helper
void GlfwErrorCallback(int error, const char* description)
{
    std::cerr << "[glfw] (" << error << ") " << description << std::endl;
}

int main(int argc, char** argv)
{
    const std::string backendName = (argc > 1) ? argv[1] : std::string();

    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "ERROR: GLFW init\n";
        return EXIT_FAILURE;
    }

    GLFWwindow* window = nullptr;
    int exitCode = EXIT_SUCCESS;

    try {
        const GraphicAPIType apiType =
            (backendName == "opengl") ? GraphicAPIType::OPENGL : GraphicAPIType::VULKAN;
        std::unique_ptr<Renderer> renderer = CreateRenderer(apiType);
        renderer->ConfigureWindowHints();

        window = glfwCreateWindow(720, 440, "Engine", NULL, NULL);
        if (!window)
            throw std::runtime_error("ERROR: GLFW window create");
        Scene scene = BuildDemo2DScene();
        std::optional<Scene> runtimeScene;
        SceneUpdater editUpdater;
        SceneUpdater runtimeUpdater;
        GlfwInput input(window);
        const CameraController cameraController;
        Editor editor;

        renderer->Init(window);

        double previousTime = glfwGetTime();

        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            const double now = glfwGetTime();
            const float deltaSeconds = static_cast<float>(now - previousTime);
            previousTime = now;

            const InputState inputState = input.Sample(editor.InputCapture());
            if (inputState.quitRequested) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            Scene& activeScene = editor.ActiveScene(scene, runtimeScene);
            cameraController.Update(activeScene.camera, inputState, deltaSeconds);
            (runtimeScene ? runtimeUpdater : editUpdater).Update(activeScene, deltaSeconds);

            const RenderSnapshot renderSnapshot = BuildRenderSnapshot(activeScene);
            renderer->DrawFrame(renderSnapshot, [&editor, &activeScene, &runtimeScene, window] {
                // 投影宽高比取实际像素尺寸；UI 鼠标坐标仍使用 ImGui 的逻辑坐标。
                int width = 0, height = 0;
                glfwGetFramebufferSize(window, &width, &height);
                editor.Draw(activeScene, runtimeScene.has_value(), glm::vec2(width, height));
            });
            const bool wasPlaying = runtimeScene.has_value();
            editor.ApplyPending(scene, runtimeScene);
            if (wasPlaying != runtimeScene.has_value()) runtimeUpdater = SceneUpdater{};
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
