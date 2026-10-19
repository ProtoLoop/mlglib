#pragma once

#include <memory>

class FileFetcher;
class GpuHelper;
class ImGuiRenderer;
class InputMapper;
class ThreadPool;

class System final
{
    class Impl;

public:
    class CreateTask;

    System() = delete;
    ~System();
    System(const System&) = delete;
    System& operator=(const System&) = delete;
    System(System&&) = delete;
    System& operator=(System&&) = delete;

    GpuHelper& GetGpuHelper();
    const GpuHelper& GetGpuHelper() const;

    FileFetcher& GetFileFetcher();
    const FileFetcher& GetFileFetcher() const;

    ThreadPool& GetThreadPool();
    const ThreadPool& GetThreadPool() const;

    const ImGuiRenderer& GetImGuiRenderer() const;

    InputMapper& GetInputMapper();
    const InputMapper& GetInputMapper() const;

    /// Posts a quit event to request the application to terminate.
    static void PostQuitEvent();

    /// Processes all pending system events, such as input, window, and focus events.
    void ProcessEvents();

    /// Captures or releases the mouse cursor. When captured, the cursor is hidden and
    /// relative mouse motion events are generated. When released, the cursor is visible and
    /// absolute mouse motion events are generated.
    /// Returns prior capture state.
    bool SetMouseCaptured(const bool captured);

    /// Returns true if the mouse is currently captured.
    bool IsMouseCaptured() const;

    /// Returns true if the window is currently minimized.
    bool IsMinimized() const;

    /// Returns true if the window was minimized during the last event processing.
    bool WasMinimized() const;

    /// Returns true if the window was restored during the last event processing.
    bool WasRestored() const;

    /// Returns true if the application should quit (e.g., if a quit event was received).
    bool ShouldQuit() const;

    /// Returns true if the application gained focus during the last event processing.
    bool WasFocusGained() const;

    /// Returns true if the application lost focus during the last event processing.
    bool WasFocusLost() const;

private:
    friend CreateTask;

    explicit System(std::unique_ptr<Impl>&& impl);

    std::unique_ptr<Impl> m_Impl;
};