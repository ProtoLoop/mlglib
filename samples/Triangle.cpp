#include "Camera.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "ResourceBundle.h"
#include "SceneTypes.h"
#include "Shell.h"
#include "System.h"
#include "View.h"

#include <imgui.h>
#include <memory>
#include <SDL3/SDL_events.h>

namespace
{
constexpr const char* kAppName = "EmTriangle";

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
    std::vector<Vertex> triangleVertices = //
        {
            { .pos{ 0.0f, 0.5f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 1, .v = 1 } } }, // 0
            { .pos{ 0.5f, 0.0f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 0, .v = 1 } } }, // 1
            { .pos{ -0.5f, 0.0f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 0, .v = 0 } } }, // 2
        };

    std::vector<VertexIndex> triangleIndices = { 0, 1, 2 };

    SceneDef sceneDef //
        {
            .ModelDefs //
            {
                {
                    .Name{ "Triangle" },
                    .MeshDefs //
                    { {
                        .Vertices{ std::move(triangleVertices) },
                        .Indices{ std::move(triangleIndices) },
                        .MaterialDef //
                        {
                            .BaseTexturePath{ "images/Ant.png" },
                            .Color{ "#FFA500"_rgba },
                            .Metalness = 0,
                            .Roughness = 0,
                        },
                    } },
                },
            },
            .NodeDefs //
            {
                {
                    .Name{ "TriangleNode" },
                    .Transform{},
                    .Model = ModelRef{ .Name{ "Triangle" } },
                },
            },
        };

    return std::move(sceneDef);
}

class TriangleApp : public ICoopTask<System&>
{
public:
    TriangleApp() = default;
    ~TriangleApp() override = default;
    TriangleApp(const TriangleApp&) = delete;
    TriangleApp& operator=(const TriangleApp&) = delete;
    TriangleApp(TriangleApp&&) = delete;
    TriangleApp& operator=(TriangleApp&&) = delete;

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

    System* m_System{ nullptr };

    std::unique_ptr<Level::CreateTask> m_LevelCreateTask;

    std::unique_ptr<Level> m_Level;

    TrTransformf m_CameraXForm{ .T{ 0, 0, -4 } };

    PerspectiveProjection m_Projection{ 1.0f };

    Stage m_Stage{ Stage::None };
};

Result<>
TriangleApp::OnStart(System& system)
{
    MLG_CHECKV(Stage::None == m_Stage, "Task has already been started");

    m_Stage = Stage::Stopped;

    m_System = &system;

    auto sceneDef = CreateScene();
    MLG_CHECK(sceneDef, "Failed to create SceneDef");

    auto createTaskResult = Level::CreateFromSceneDef(system, std::move(*sceneDef));
    MLG_CHECK(createTaskResult, "Failed to create level from scene definition");

    m_LevelCreateTask = std::move(*createTaskResult);

    m_Stage = Stage::CreatingLevel;

    return Result<>::Ok;
}

void
TriangleApp::OnUpdate()
{
    MLG_ASSERT(m_System);

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
                    m_Stage = TriangleApp::Stage::Running;
                }

                m_LevelCreateTask.reset();
            }
            break;

        case Stage::Running:
            if(m_System->ShouldQuit())
            {
                m_Stage = Stage::Stopped;
            }
            else if(!m_System->IsMinimized())
            {
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
TriangleApp::Render()
{
    MLG_ASSERT(m_System);

    const GpuHelper& gpuHelper = m_System->GetGpuHelper();

    const Dimension2 screenDims = gpuHelper.GetScreenDimensions();

    m_Projection.SetAspectRatio(screenDims.GetAspectRatio());

    const Camera camera(m_Projection, m_CameraXForm);
    MLG_CHECK(m_Level->GetView().Render(camera, screenDims), "Failed to render view");

    auto target = gpuHelper.GetSwapChainTexture();
    MLG_CHECK(target, "Failed to get swap chain texture");

    MLG_CHECK(m_Level->GetView().Composite(*target), "Failed to composite view");

    MLG_CHECK(m_System->GetImGuiRenderer().Render(gpuHelper.GetDevice(), *target, RenderGui),
        "Failed to render ImGui");

    return Result<>::Ok;
}

void
Run()
{
    static TriangleApp app;
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