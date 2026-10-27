#include "CameraActor.h"
#include "CommonActionIds.h"
#include "GpuColorPass.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "InputMapper.h"
#include "Level.h"
#include "PerfMetrics.h"
#include "System.h"
#include "SystemCreateTask.h"
#include "VecMath.h"
#include "View.h"

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <string>
#include <thread>

namespace
{
constexpr const char* kAppName = "Viewer";

Result<>
RenderGui()
{
#if defined(NDEBUG)
    constexpr const char* buildType = "Release";
#else
    constexpr const char* buildType = "Debug";
#endif

    constexpr const char* backend = "Dawn";

    auto title = std::format("Counters: {}/{}", buildType, backend);

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(title.c_str());

    constexpr size_t kMaxPerfStats = 256;

    const PerfStats* perfStats[kMaxPerfStats];
    std::span<const PerfStats*> perfStatsSpan(perfStats);

    // Timers
    size_t counterCount = PerfMetrics::SampleCounters<PerfTimerCategory>(perfStatsSpan);

    std::span<const PerfStats*> sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters, {}, &PerfStats::GetName);

    for(const auto* counterStat : sortedCounters)
    {
        const auto text =
            InplaceString<>::Format("{}: {:.3f} ms", counterStat->GetName(), counterStat->GetEMA());
        ImGui::TextUnformatted(text.c_str());
    }

    // Other counters
    counterCount = PerfMetrics::SampleCounters<>(perfStatsSpan);

    sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters, {}, &PerfStats::GetName);

    for(const auto* counterStat : sortedCounters)
    {
        const auto text =
            InplaceString<>::Format("{}: {:.3f}", counterStat->GetName(), counterStat->GetEMA());
        ImGui::TextUnformatted(text.c_str());
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<std::unique_ptr<Level>>
LoadLevel(System& system, const FilePath& path)
{
    const std::string_view extOrig = path.GetExtension();
    char buf[FilePath::StringStorageType::kMaxLength];

    const std::span<char> destination(buf);

    std::ranges::transform(extOrig,
        destination.begin(),
        [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });

    const std::string_view ext(destination.data(), extOrig.size());

    std::unique_ptr<Level::CreateTask> createTask;

    if(ext == ".gltf")
    {
        auto createTaskResult = Level::CreateFromGltf(system, path);
        MLG_CHECK(createTaskResult, "Failed to create level from glTF");

        createTask = std::move(*createTaskResult);
    }

    if(ext == ".bin")
    {
        auto createTaskResult = Level::CreateFromBundle(system, path);
        MLG_CHECK(createTaskResult, "Failed to create level from bundle");

        createTask = std::move(*createTaskResult);
    }

    while(createTask->IsRunning())
    {
        system.GetFileFetcher().ProcessCompletions();
        createTask->Update();
    }

    return createTask->Take();
}

// constexpr const char* SPONZA_PATH = "main_sponza/NewSponza_Main_glTF_003.gltf";
constexpr const char* SPONZA_PATH = "main_sponza_bin/NewSponza_Main_glTF_003.bin";

Result<>
MainLoop()
{
    System::CreateTask sysCreateTask;

    MLG_CHECK(sysCreateTask.Start(kAppName));

    while(sysCreateTask.IsRunning())
    {
        sysCreateTask.Update();
    }

    auto systemResult = sysCreateTask.Take();
    MLG_CHECK(systemResult, "Failed to create System");
    std::unique_ptr<System> system = std::move(*systemResult);

    const auto sponzaModelPath = DirectoryPath::GetCurrentDirectory().Join(SPONZA_PATH);
    MLG_CHECK(sponzaModelPath, "Failed to get sponza model path");

    auto loadResult = LoadLevel(*system, *sponzaModelPath);
    MLG_CHECK(loadResult, "Failed to load resources");

    const std::unique_ptr<Level> level = std::move(*loadResult);

    View& view = level->GetView();

    static constexpr float kDefaultCameraHeight = 2.0f;
    static constexpr float kDefaultCameraYaw = 90.0f; // Degrees

    const Radiansf cameraYaw = Radiansf::FromDegrees(kDefaultCameraYaw);

    const GpuHelper& gpuHelper = system->GetGpuHelper();

    Dimension2 screenDims = gpuHelper.GetScreenDimensions();

    CameraActor cameraActor(system->GetInputMapper());
    cameraActor.SetTransform(
        TrTransformf{ .T{ 0, kDefaultCameraHeight, 0 }, .R{ cameraYaw, Vec3f::YAXIS() } });
    cameraActor.SetAspectRatio(screenDims.GetAspectRatio());

    constexpr ActionMapping actionMappings[] //
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

    MLG_CHECK(system->GetInputMapper().AddActionMappings(actionMappings));

    Timer frameTimer;

    bool isCameraActorActive = false;

    while(!system->ShouldQuit())
    {
        MLG_SCOPED_TIMER("Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        system->ProcessEvents();

        if(system->IsMinimized())
        {
            std::this_thread::yield();
            continue;
        }

        if(system->ShouldQuit())
        {
            break;
        }

        const InputMapper& inputMapper = system->GetInputMapper();

        if(inputMapper.IsActionTriggered(CommonActionIds::Quit))
        {
            System::PostQuitEvent();
        }
        if(inputMapper.IsActionTriggered(CommonActionIds::CaptureMouse))
        {
            isCameraActorActive = true;
            system->SetMouseCaptured(true);
        }
        if(inputMapper.IsActionTriggered(CommonActionIds::ReleaseMouse))
        {
            isCameraActorActive = false;
            system->SetMouseCaptured(false);
        }

        const Dimension2 curScreenDims = gpuHelper.GetScreenDimensions();

        if(curScreenDims != screenDims)
        {
            cameraActor.SetAspectRatio(curScreenDims.GetAspectRatio());
            screenDims = curScreenDims;
        }

        if(isCameraActorActive)
        {
            cameraActor.Update(inputMapper, elapsedSeconds);
        }

        auto target = gpuHelper.GetSwapChainTexture();
        MLG_CHECKV(target, "Failed to get swap chain texture");

        MLG_CHECK(view.Render(cameraActor.GetCamera(), curScreenDims));
        MLG_CHECK(view.Composite(*target));

        const ImGuiRenderer& imGuiRenderer = system->GetImGuiRenderer();
        MLG_CHECK(imGuiRenderer.Render(gpuHelper.GetDevice(), *target, RenderGui));

        MLG_CHECK(gpuHelper.Present(), "Failed to present backbuffer");
    }

    PerfMetrics::LogCounters();

    return Result<>::Ok;
}

} // namespace

int
main(int /*argc*/, char** /*argv*/)
{
    Log::SetLevel(Log::Level::Trace);

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}