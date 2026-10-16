#include "System.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "InputMapper.h"
#include "SystemCreateTask.h"
#include "ThreadPool.h"

#include <imgui_impl_sdl3.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <utility>

namespace
{
enum class FocusEvent
{
    None,
    Gained,
    Lost
};

enum class WindowStateEvent
{
    None,
    Minimized,
    Restored
};
} // namespace

class System::Impl
{
public:
    std::unique_ptr<GpuHelper> m_GpuHelper;
    std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
    FileFetcher m_FileFetcher;
    ThreadPool m_ThreadPool;
    InputMapper m_InputMapper;

    FocusEvent m_FocusEvent{ FocusEvent::None };
    WindowStateEvent m_WindowStateEvent{ WindowStateEvent::None };

    bool m_Minimized{ false };
    bool m_ShouldQuit{ false };
};

System::CreateTask::CreateTask()
    : m_Impl(std::make_unique<Impl>())
{
}

System::CreateTask::~CreateTask() = default;

Result<std::unique_ptr<System>>
System::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(m_Impl, "Task result already consumed");

    auto gpuHelperResult = m_GpuHelperTask.Take();
    MLG_CHECK(gpuHelperResult, "Failed create GpuHelper");
    m_Impl->m_GpuHelper = std::move(*gpuHelperResult);

    auto imGuiResult = ImGuiRenderer::Create(*m_Impl->m_GpuHelper);
    MLG_CHECK(imGuiResult, "Failed to create ImGuiRenderer");
    m_Impl->m_ImGuiRenderer = std::move(*imGuiResult);

    return std::unique_ptr<System>(new System(std::move(m_Impl)));
}

// private:

Result<>
System::CreateTask::OnStart(const std::string_view appName)
{
    MLG_CHECKV(m_Stage == Stage::None, "Task is already in progress");

    // Set the initial stage to failed to ensure that any early exit will mark the task as failed.
    m_Stage = Stage::Failed;

    MLG_DEBUG("Creating System...");

    MLG_CHECK(m_GpuHelperTask.Start(appName), "Failed to start GpuHelper creation");

    m_Stage = Stage::CreatingGpuHelper;

    return Result<>::Ok;
}

void
System::CreateTask::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::CreatingGpuHelper:
            if(m_GpuHelperTask.IsRunning())
            {
                m_GpuHelperTask.Update();
            }
            else
            {
                m_Stage = Stage::Succeeded;
            }
            break;

        case Stage::Failed:
            MLG_ERROR("System creation failed");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}

////////// System

System::System(std::unique_ptr<Impl>&& impl)
    : m_Impl(std::move(impl))
{
}

System::~System() = default;

GpuHelper&
System::GetGpuHelper()
{
    return *m_Impl->m_GpuHelper;
}

const GpuHelper&
System::GetGpuHelper() const
{
    return *m_Impl->m_GpuHelper;
}

FileFetcher&
System::GetFileFetcher()
{
    return m_Impl->m_FileFetcher;
}

const FileFetcher&
System::GetFileFetcher() const
{
    return m_Impl->m_FileFetcher;
}

ThreadPool&
System::GetThreadPool()
{
    return m_Impl->m_ThreadPool;
}

const ThreadPool&
System::GetThreadPool() const
{
    return m_Impl->m_ThreadPool;
}

const ImGuiRenderer&
System::GetImGuiRenderer() const
{
    return *m_Impl->m_ImGuiRenderer;
}

InputMapper&
System::GetInputMapper()
{
    return m_Impl->m_InputMapper;
}

const InputMapper&
System::GetInputMapper() const
{
    return m_Impl->m_InputMapper;
}

void
System::PostQuitEvent()
{
    SDL_Event event;
    event.quit = SDL_QuitEvent //
        {
            .type = SDL_EVENT_QUIT,
            .timestamp = SDL_GetTicksNS(),
        };

    SDL_PushEvent(&event);
}

void
System::ProcessEvents()
{
    GetGpuHelper().GetInstance().ProcessEvents();
    GetFileFetcher().ProcessCompletions();

    m_Impl->m_FocusEvent = FocusEvent::None;
    m_Impl->m_WindowStateEvent = WindowStateEvent::None;

    m_Impl->m_InputMapper.BeginFrame();

    SDL_Event sdlEvent;
    while(SDL_PollEvent(&sdlEvent))
    {
        ImGui_ImplSDL3_ProcessEvent(&sdlEvent);

        switch(sdlEvent.type)
        {
            case SDL_EVENT_QUIT:
                m_Impl->m_ShouldQuit = true;
                break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                m_Impl->m_Minimized = false;
                m_Impl->m_WindowStateEvent = WindowStateEvent::Restored;
                m_Impl->m_InputMapper.Clear();
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                m_Impl->m_Minimized = true;
                m_Impl->m_WindowStateEvent = WindowStateEvent::Minimized;
                m_Impl->m_InputMapper.Clear();
                break;

            default:
                break;
        }

        if(IsMinimized() || ShouldQuit())
        {
            continue;
        }

        switch(sdlEvent.type)
        {
            // case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                const uint32_t newWidth = static_cast<uint32_t>(sdlEvent.window.data1);
                const uint32_t newHeight = static_cast<uint32_t>(sdlEvent.window.data2);
                if(!MLG_VERIFY(GetGpuHelper().Resize(newWidth, newHeight)))
                {
                    PostQuitEvent();
                }
            }
            break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                m_Impl->m_Minimized = false;
                break;

            // case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                m_Impl->m_FocusEvent = FocusEvent::Gained;
                m_Impl->m_InputMapper.Clear();
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                m_Impl->m_FocusEvent = FocusEvent::Lost;
                m_Impl->m_InputMapper.Clear();
                break;

            case SDL_EVENT_KEY_DOWN:
                m_Impl->m_InputMapper.OnButtonPressed(InputButtonDevice::Keyboard,
                    static_cast<unsigned>(sdlEvent.key.scancode));
                break;

            case SDL_EVENT_KEY_UP:
                m_Impl->m_InputMapper.OnButtonReleased(InputButtonDevice::Keyboard,
                    static_cast<unsigned>(sdlEvent.key.scancode));
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                m_Impl->m_InputMapper.OnButtonPressed(InputButtonDevice::Mouse,
                    static_cast<unsigned>(sdlEvent.button.button));
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                m_Impl->m_InputMapper.OnButtonReleased(InputButtonDevice::Mouse,
                    static_cast<unsigned>(sdlEvent.button.button));
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if(sdlEvent.wheel.x != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::MouseWheel,
                        InputAxisIdentifier::X,
                        sdlEvent.wheel.x);
                }

                if(sdlEvent.wheel.y != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::MouseWheel,
                        InputAxisIdentifier::Y,
                        sdlEvent.wheel.y);
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if(sdlEvent.motion.xrel != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::Mouse,
                        InputAxisIdentifier::X,
                        sdlEvent.motion.xrel);
                }

                if(sdlEvent.motion.yrel != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::Mouse,
                        InputAxisIdentifier::Y,
                        sdlEvent.motion.yrel);
                }
                break;

            default:
                break;
        }
    }

    m_Impl->m_InputMapper.EndFrame();
}

bool
System::SetMouseCaptured(const bool captured)
{
    const bool wasCaptured = IsMouseCaptured();
    SDL_SetWindowRelativeMouseMode(GetGpuHelper().GetWindow(), captured);

    return wasCaptured;
}

bool
System::IsMouseCaptured() const
{
    return SDL_GetWindowRelativeMouseMode(GetGpuHelper().GetWindow());
}

bool
System::IsMinimized() const
{
    return m_Impl->m_Minimized;
}

bool
System::WasMinimized() const
{
    return m_Impl->m_WindowStateEvent == WindowStateEvent::Minimized;
}

bool
System::WasRestored() const
{
    return m_Impl->m_WindowStateEvent == WindowStateEvent::Restored;
}

bool
System::ShouldQuit() const
{
    return m_Impl->m_ShouldQuit;
}

bool
System::WasFocusGained() const
{
    return m_Impl->m_FocusEvent == FocusEvent::Gained;
}

bool
System::WasFocusLost() const
{
    return m_Impl->m_FocusEvent == FocusEvent::Lost;
}