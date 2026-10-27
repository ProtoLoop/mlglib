#include "CameraActor.h"
#include "CommonActionIds.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "ResourceBundle.h"
#include "SceneTypes.h"
#include "ShapeDefs.h"
#include "Shell.h"
#include "System.h"
#include "VecMath.h"
#include "View.h"

#include <imgui.h>
#include <memory>
#include <optional>
#include <SDL3/SDL_events.h>

namespace
{
constexpr const char* kAppName = "Translucent";

Result<>
RenderGui()
{
    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin("Counters");

    constexpr size_t kMaxPerfStats = 256;

    const PerfStats* perfStats[kMaxPerfStats];
    std::span<const PerfStats*> perfStatsSpan(perfStats);
    const size_t counterCount = PerfMetrics::SampleCounters(perfStatsSpan);
    for(const auto* counterStat : perfStatsSpan.first(counterCount))
    {
        const char* units = counterStat->GetCategoryId() == PerfTimerCategory::Id ? "ms" : "";
        const auto text = InplaceString<>::Format("{}: {:.3f} {}",
            counterStat->GetName(),
            counterStat->GetEMA(),
            units);
        ImGui::TextUnformatted(text.c_str());
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<SceneDef>
CreateScene()
{
    MeshDef meshDef = ShapeDefs::Box({ .Width = 1.0f, .Height = 1.0f, .Depth = 1.0f });
    // MeshDef meshDef = ShapeDefs::Ball({ .Radius = 1.0f });

    meshDef.MaterialDef = MaterialDef //
        {
            .BaseTexturePath{ "images/StainedGlass.png" },
            .Color{ "#FFFFFF"_rgba },
            .AlphaMode = AlphaMode::Blend,
            .Metalness = 0,
            .Roughness = 0,
        };

    ModelDef model //
        {
            .Name{ "Model" },
            .MeshDefs //
            {
                meshDef,
            },
        };

    SceneDef sceneDef //
        { .ModelDefs  //
            {
                model,
            },
            .NodeDefs //
            {
                {
                    .Name{ "Node1" },
                    .Transform{ .T = { 0, 0, 4 } },
                    .Model = ModelRef{ .Name{ "Model" } },
                },
                {
                    .Name{ "Node2" },
                    .Transform{},
                    .Model = ModelRef{ .Name{ "Model" } },
                },
            } };

    return std::move(sceneDef);
}

class TranslucentApp : public ICoopTask<System&>
{
public:
    TranslucentApp() = default;
    ~TranslucentApp() override = default;
    TranslucentApp(const TranslucentApp&) = delete;
    TranslucentApp& operator=(const TranslucentApp&) = delete;
    TranslucentApp(TranslucentApp&&) = delete;
    TranslucentApp& operator=(TranslucentApp&&) = delete;

private:
    enum class Stage
    {
        None,
        CreatingLevel,
        Running,
        Stopped
    };

    Result<> OnStart(System& system) override;

    void OnUpdate() override;

    Result<> Render();

    Result<> AddActionMappings();

    System* m_System{ nullptr };

    std::unique_ptr<Level::CreateTask> m_LevelCreateTask;

    std::unique_ptr<Level> m_Level;

    std::optional<CameraActor> m_CameraActor;
    Timer m_FrameTimer;
    bool m_IsCameraActorActive{ false };

    Stage m_Stage{ Stage::None };
};

Result<>
TranslucentApp::OnStart(System& system)
{
    MLG_CHECKV(Stage::None == m_Stage, "Task has already been started");

    m_Stage = Stage::Stopped;

    m_System = &system;

    auto sceneDef = CreateScene();
    MLG_CHECK(sceneDef, "Failed to create SceneDef");

    auto createTaskResult = Level::CreateFromSceneDef(system, std::move(*sceneDef));
    MLG_CHECK(createTaskResult, "Failed to create level from scene definition");

    m_LevelCreateTask = std::move(*createTaskResult);

    m_CameraActor.emplace(system.GetInputMapper());

    m_Stage = Stage::CreatingLevel;

    return Result<>::Ok;
}

void
TranslucentApp::OnUpdate()
{
    MLG_ASSERT(m_System);

    const float elapsedSeconds = m_FrameTimer.GetElapsedSeconds();

    m_FrameTimer.Restart();

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::CreatingLevel:
            if(m_LevelCreateTask->IsRunning())
            {
                m_LevelCreateTask->Update();
            }
            else
            {
                auto levelResult = m_LevelCreateTask->Take();

                if(!MLG_VERIFY(levelResult, "Failed to create Level"))
                {
                    m_Stage = Stage::Stopped;
                }
                else
                {
                    m_Level = std::move(*levelResult);
                    m_CameraActor->SetTransform(TrTransformf{ .T = { 0, 0, -4 } });
                    if(!AddActionMappings())
                    {
                        MLG_ERROR("Failed to add action mappings");
                        m_Stage = Stage::Stopped;
                    }
                    else
                    {
                        m_Stage = Stage::Running;
                    }

                    m_LevelCreateTask.reset();
                }
            }
            break;

        case Stage::Running:
            if(m_System->ShouldQuit())
            {
                m_Stage = Stage::Stopped;
            }
            else if(!m_System->IsMinimized())
            {
                const InputMapper& inputMapper = m_System->GetInputMapper();

                if(inputMapper.IsActionTriggered(CommonActionIds::Quit))
                {
                    System::PostQuitEvent();
                }
                if(inputMapper.IsActionTriggered(CommonActionIds::CaptureMouse))
                {
                    m_IsCameraActorActive = true;
                    m_System->SetMouseCaptured(true);
                }
                if(inputMapper.IsActionTriggered(CommonActionIds::ReleaseMouse))
                {
                    m_IsCameraActorActive = false;
                    m_System->SetMouseCaptured(false);
                }

                if(m_IsCameraActorActive)
                {
                    m_CameraActor->Update(m_System->GetInputMapper(), elapsedSeconds);
                }

                if(!MLG_VERIFY(Render(), "Failed to render view"))
                {
                    m_Stage = Stage::Stopped;
                }
            }
            break;

        case Stage::Stopped:
            SetComplete();
            break;
    }
}

Result<>
TranslucentApp::Render()
{
    MLG_ASSERT(m_System);

    const GpuHelper& gpuHelper = m_System->GetGpuHelper();

    const Dimension2 screenDims = gpuHelper.GetScreenDimensions();

    m_CameraActor->SetAspectRatio(screenDims.GetAspectRatio());

    MLG_CHECK(m_Level->GetView().Render(m_CameraActor->GetCamera(), screenDims),
        "Failed to render view");

    auto targetResult = gpuHelper.GetSwapChainTexture();
    MLG_CHECK(targetResult, "Failed to get swap chain texture");

    MLG_CHECK(m_Level->GetView().Composite(*targetResult), "Failed to composite view");

    MLG_CHECK(m_System->GetImGuiRenderer().Render(gpuHelper.GetDevice(), *targetResult, RenderGui),
        "Failed to render ImGui");

    return Result<>::Ok;
}

Result<>
TranslucentApp::AddActionMappings()
{
    static constexpr ActionMapping actionMappings[] //
        {
            {
                .ActionId = CommonActionIds::Quit,
                .Trigger = InputButton::KeyPressed(SDL_SCANCODE_ESCAPE),
            },
            {
                .ActionId = CommonActionIds::CaptureMouse,
                .Trigger = InputButton::MousePressed(SDL_BUTTON_LEFT),
            },
            {
                .ActionId = CommonActionIds::ReleaseMouse,
                .Trigger = InputButton::MouseReleased(SDL_BUTTON_LEFT),
            },
        };

    return m_System->GetInputMapper().AddActionMappings(actionMappings);
}

void
Run()
{
    static TranslucentApp app;
    static Shell shell(app);
    static bool started = false;

    if(!started)
    {
        started = true;
        if(!shell.Start(kAppName))
        {
            MLG_ERROR("Failed to start Shell");
            return;
        }
    }

    if(shell.IsRunning())
    {
        shell.Update();
    }
    else
    {
        emscripten_cancel_main_loop();
    }
}

} // namespace

int
main(int /*argc*/, char** /*argv*/)
{
    Log::SetLevel(Log::Level::Trace);

    emscripten_set_main_loop(Run, 0, 1);

    return 0;
}