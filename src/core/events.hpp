#pragma once

#include "keycodes.hpp"

#define BIT(x) (1 << x)
#define BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

enum class EventType
{
    None = 0,
    WindowClosed, WindowResized, WindowMinimize,
    KeyPressed, KeyReleased, KeyTyped,
    MouseButtonPressed, MouseButtonReleased,
    MouseMoved, MouseScrolled
};

enum EventCategory : i32
{
    None = 0,
    EventCategoryApplication    = BIT(0),
    EventCategoryInput          = BIT(1),
    EventCategoryKeyboard       = BIT(2),
    EventCategoryMouseButton    = BIT(3),
    EventCategoryMouse          = BIT(4)
};

struct Event
{
    mutable bool handled { false };

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::None;
    }

    virtual constexpr auto type() const -> EventType
    {
        return static_type();
    }

    virtual constexpr auto category() const -> i32
    {
        return EventCategory::None;
    }

    constexpr auto is_category(EventCategory category) const -> bool
    {
        return category & (this->category());
    }
};

struct WindowClosedEvent final : public Event
{
    constexpr explicit WindowClosedEvent() noexcept = default;

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::WindowClosed;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }

    virtual constexpr auto category() const -> i32 override
    {
        return EventCategory::EventCategoryApplication;
    }
};

struct WindowResizedEvent final : public Event
{
    u32 width;
    u32 height;

    constexpr explicit WindowResizedEvent(u32 width, u32 height) noexcept
        : width(width), height(height)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::WindowResized;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }

    virtual constexpr auto category() const -> i32 override
    {
        return EventCategory::EventCategoryApplication;
    }
};

struct WindowMinimizeEvent final : public Event
{
    bool minimized;

    constexpr explicit WindowMinimizeEvent(bool minimized) noexcept
        : minimized(minimized)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::WindowMinimize;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }

    virtual constexpr auto category() const -> i32 override
    {
        return EventCategory::EventCategoryApplication;
    }
};

struct KeyEvent : public Event
{
    KeyCode keycode;

    virtual constexpr auto category() const -> i32 override
    {
        return EventCategory::EventCategoryInput | EventCategory::EventCategoryKeyboard;
    }

protected:
    constexpr KeyEvent(KeyCode keycode) noexcept
        : keycode(keycode)
    {
    }
};

struct KeyPressedEvent final : public KeyEvent
{
    bool repeat;

    constexpr explicit KeyPressedEvent(KeyCode keycode, bool repeat) noexcept
        : KeyEvent(keycode), repeat(repeat)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::KeyPressed;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }
};

struct KeyReleasedEvent final : public KeyEvent
{
    constexpr explicit KeyReleasedEvent(KeyCode keycode) noexcept
        : KeyEvent(keycode)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::KeyReleased;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }
};

struct KeyTypedEvent final : public Event
{
    u32 codepoint;

    constexpr explicit KeyTypedEvent(u32 codepoint) noexcept
        : codepoint(codepoint)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::KeyTyped;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }

    virtual constexpr auto category() const -> i32 override
    {
        return EventCategory::EventCategoryInput | EventCategory::EventCategoryKeyboard;
    }
};

struct MouseButtonEvent : public Event
{
    MouseButton button;

    virtual constexpr auto category() const -> i32 override
    {
        return EventCategory::EventCategoryInput | EventCategory::EventCategoryMouseButton;
    }

protected:
    constexpr MouseButtonEvent(MouseButton button) noexcept
        : button(button)
    {
    }
};

struct MouseButtonPressedEvent final : public MouseButtonEvent
{
    constexpr explicit MouseButtonPressedEvent(MouseButton button) noexcept
        : MouseButtonEvent(button)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::MouseButtonPressed;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }
};

struct MouseButtonReleasedEvent final : public MouseButtonEvent
{
    constexpr explicit MouseButtonReleasedEvent(MouseButton button) noexcept
        : MouseButtonEvent(button)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::MouseButtonReleased;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }
};

struct MouseEvent : public Event
{
    f32 x;
    f32 y;

    virtual constexpr auto category() const -> i32 override
    {
        return EventCategory::EventCategoryInput | EventCategory::EventCategoryMouse;
    }

protected:
    constexpr MouseEvent(f32 x, f32 y) noexcept
        : x(x), y(y)
    {
    }
};

struct MouseMovedEvent final : public MouseEvent
{
    constexpr explicit MouseMovedEvent(f32 x, f32 y) noexcept
        : MouseEvent(x, y)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::MouseMoved;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }
};

struct MouseScrolledEvent final : public MouseEvent
{
    constexpr explicit MouseScrolledEvent(f32 x, f32 y) noexcept
        : MouseEvent(x, y)
    {
    }

    inline static constexpr auto static_type() -> EventType
    {
        return EventType::MouseScrolled;
    }

    virtual constexpr auto type() const -> EventType override
    {
        return static_type();
    }
};

template <typename T>
concept IsEvent = std::is_base_of_v<Event, T>;

template <typename TFn, typename TEvent>
concept EventHandler = requires (TFn fn, const TEvent& event)
{
    { TEvent::static_type() } -> std::same_as<EventType>;
    { std::invoke(std::forward<TFn>(fn), event) } -> std::same_as<bool>;
};

class EventDispatcher
{
public:
    constexpr explicit EventDispatcher(const Event& event) noexcept
        : m_event(event)
    {
    }

    template <IsEvent T, EventHandler<T> Fn>
    auto dispatch(Fn&& fn) const noexcept -> void
    {
        if (m_event.handled) return;
        if (m_event.type() == T::static_type()) {
            m_event.handled |= std::invoke(std::forward<Fn>(fn), static_cast<const T&>(m_event));
        }
    }

    template <IsEvent T>
    auto block() const noexcept -> void
    {
        if (m_event.type() == T::static_type()) {
            m_event.handled = true;
        }
    }

private:
    const Event& m_event;
};
