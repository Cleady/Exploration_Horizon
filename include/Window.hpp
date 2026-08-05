#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

using MousePosCallback = std::function<void(double xpos, double ypos)>;
using ScrollCallback = std::function<void(double xoffset, double yoffset)>;

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool init();
    bool shouldClose() const;
    void update();
    void processInput();

    void setMouseCallback(MousePosCallback callback);
    void setScrollCallback(ScrollCallback callback);
    void setCursorDisabled(bool disabled);

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    GLFWwindow* getGLFWwindow() const { return m_window; }

private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void mousePosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    int m_width;
    int m_height;
    std::string m_title;
    GLFWwindow* m_window;

    MousePosCallback m_userMouseCallback;
    ScrollCallback m_userScrollCallback;
};

#endif // WINDOW_HPP
