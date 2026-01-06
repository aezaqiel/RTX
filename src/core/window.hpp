#pragma once

#include "events.hpp"

struct GLFWwindow;

class Window
{
    friend class Application;
    using EventCallbackFn = std::function<void(const Event&)>;
public:
    Window(u32 width, u32 height, const std::string& title);
    ~Window();

    inline auto title() const -> std::string
    {
        return m_data.title;
    }

    inline auto width() const -> u32
    {
        return m_data.width;
    }

    inline auto height() const -> u32
    {
        return m_data.height;
    }

    inline auto native() const -> GLFWwindow*
    {
        return m_window;
    }

    inline auto bind_event_callback(EventCallbackFn&& callback) -> void
    {
        m_data.callback = std::forward<EventCallbackFn>(callback);
    }

protected:
    static auto poll_events() -> void;

private:
    struct WindowData
    {
        std::string title;
        u32 width { 0 };
        u32 height { 0 };

        EventCallbackFn callback { nullptr };
    };

private:
    inline static std::atomic<usize> s_instance { 0 };

    GLFWwindow* m_window { nullptr };
    WindowData m_data;
};
