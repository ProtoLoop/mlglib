#define MLG_LOGGER_NAME "LEVL"

#include "Level.h"

#include "GltfLoader.h"
#include "ResourceBundle.h"
#include "Scene.h"
#include "System.h"

#include <llex.h>

/// Level

Level::~Level() = default;

Result<std::unique_ptr<Level::CreateTask>> Level::CreateFromSceneDef(System& system, SceneDef&& sceneDef)
{
    std::unique_ptr<CreateTask> task(new CreateTask(system, std::move(sceneDef)));
    MLG_CHECK(task->Start(), "Failed to start level creation task");
    return task;
}

Result<std::unique_ptr<Level::CreateTask>> Level::CreateFromBundle(System& system, const FilePath& bundlePath)
{
    std::unique_ptr<CreateTask> task(new CreateTask(system, CreateTask::BundleParams{ .Path = bundlePath }));
    MLG_CHECK(task->Start(), "Failed to start level creation task");
    return task;
}

Result<std::unique_ptr<Level::CreateTask>> Level::CreateFromGltf(System& system, const FilePath& gltfPath)
{
    std::unique_ptr<CreateTask> task(new CreateTask(system, CreateTask::GltfParams{ .Path = gltfPath }));
    MLG_CHECK(task->Start(), "Failed to start level creation task");
    return task;
}

Level::Level(std::unique_ptr<Scene>&& scene, std::unique_ptr<View>&& view)
    : m_Scene(std::move(scene)),
      m_View(std::move(view))
{
    MLG_ABORTIF(!m_Scene || !m_View, "Scene or View is null");
}

/// Level::CreateTask

Level::CreateTask::CreateTask(System& system, SceneDef&& sceneDef)
    : m_System(&system),
      m_Params(std::move(sceneDef)),
      m_Stage(Stage::StartFromSceneDef)
{
}

Level::CreateTask::CreateTask(System& system, BundleParams bundleParams)
    : m_System(&system),
      m_Params(std::move(bundleParams)),
      m_Stage(Stage::StartFromBundleParams)
{
}

Level::CreateTask::CreateTask(System& system, GltfParams gltfParams)
    : m_System(&system),
      m_Params(std::move(gltfParams)),
      m_Stage(Stage::StartFromGltfParams)
{
}

Level::CreateTask::~CreateTask() = default;

Result<std::unique_ptr<Level>>
Level::CreateTask::Take()
{
    MLG_CHECK(!IsRunning(), "Task is still running");
    MLG_CHECK(Stage::Succeeded == m_Stage, "Task has not succeeded");
    MLG_CHECKV(m_Level, "Level has already been taken");

    auto bye = std::move(m_Level);
    m_Level.reset();

    return bye;
}

Result<>
Level::CreateTask::OnStart()
{
    m_Timer.Restart();

    MLG_DEBUG("Loading level...");

    MLG_CHECKV(Stage::StartFromSceneDef == m_Stage
            || Stage::StartFromBundleParams == m_Stage
            || Stage::StartFromGltfParams == m_Stage,
        "Invalid initial stage");

    if(Stage::StartFromSceneDef == m_Stage)
    {
        MLG_DEBUG("Loading level from SceneDef...");
    }
    else
    {
        auto pathResult = GetPath();
        MLG_CHECK(pathResult, "Failed to get path for level");

        MLG_DEBUG("Loading level from: {}...", *pathResult);
    }

    return Result<>::Ok;
}

void
Level::CreateTask::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::StartFromSceneDef:
            MLG_DEBUG("Creating level from SceneDef...");
            if(auto bundleResult = ResourceBundleBuilder().Build(std::get<SceneDef>(m_Params),
                   ResourceBundleBuilder::Options{
                       .TextureEncodings = TextureResourceEncoding::None });
                !bundleResult)
            {
                MLG_ERROR("Failed to create resource bundle from SceneDef");
                m_Stage = Stage::Failed;
            }
            else if(auto sceneResult = Scene::Create(*bundleResult); !sceneResult)
            {
                MLG_ERROR("Failed to create scene from resource bundle");
                m_Stage = Stage::Failed;
            }
            else
            {
                // Must keep the resource bundle alive while the view is being created
                m_ResourceBundle = std::move(*bundleResult);
                m_Scene = std::move(*sceneResult);

                if(!CreateView(DirectoryPath::GetCurrentDirectory(), m_ResourceBundle, *m_Scene))
                {
                    MLG_ERROR("Failed to create view");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Stage = Stage::CreatingView;
                }
            }
            break;

        case Stage::StartFromBundleParams:
            if(auto pathResult = GetPath(); !pathResult)
            {
                MLG_ERROR("Failed to get path for bundle file");
                m_Stage = Stage::Failed;
            }
            else
            {
                MLG_DEBUG("Creating level from {}...", *pathResult);

                if(auto loadResult = ResourceBundle::Load(*pathResult, m_System->GetFileFetcher()); !loadResult)
                {
                    MLG_ERROR("Failed to load resource bundle: {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_RsrcBundleLoadTask = std::move(*loadResult);
                    m_Stage = Stage::LoadingBundle;
                }
            }
            break;

        case Stage::StartFromGltfParams:
            if(auto pathResult = GetPath(); !pathResult)
            {
                MLG_ERROR("Failed to get path for glTF file");
                m_Stage = Stage::Failed;
            }
            else
            {
                MLG_DEBUG("Creating level from {}...", *pathResult);

                if(const auto loadResult = GltfLoader::Load(*pathResult); !loadResult)
                {
                    MLG_ERROR("Failed to load glTF file: {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else if(auto bundleResult = ResourceBundleBuilder().Build(*loadResult);
                    !bundleResult)
                {
                    MLG_ERROR("Failed to create resource bundle: {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else if(auto sceneResult = Scene::Create(*bundleResult); !sceneResult)
                {
                    MLG_ERROR("Failed to create scene from resource bundle: {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else if(const auto parentPathResult = DirectoryPath::GetParentPath(*pathResult);
                    !parentPathResult)
                {
                    MLG_ERROR("Failed to get parent path for {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_ResourceBundle = std::move(*bundleResult);
                    m_Scene = std::move(*sceneResult);

                    if(!CreateView(*parentPathResult, m_ResourceBundle, *m_Scene))
                    {
                        MLG_ERROR("Failed to create view");
                        m_Stage = Stage::Failed;
                    }
                    else
                    {
                        m_Stage = Stage::CreatingView;
                    }
                }
            }
            break;
        case Stage::LoadingBundle:
            if(m_RsrcBundleLoadTask->IsRunning())
            {
                m_RsrcBundleLoadTask->Update();
            }
            else
            {
                if(auto pathResult = GetPath(); !pathResult)
                {
                    MLG_ERROR("Failed to get path for resource bundle");
                    m_Stage = Stage::Failed;
                }
                else if(auto bundleResult = m_RsrcBundleLoadTask->Take(); !bundleResult)
                {
                    MLG_ERROR("Failed to load resource bundle from path {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else if(auto sceneResult = Scene::Create(*bundleResult); !sceneResult)
                {
                    MLG_ERROR("Failed to create scene from resource bundle: {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else if(const auto parentPathResult = DirectoryPath::GetParentPath(*pathResult);
                    !parentPathResult)
                {
                    MLG_ERROR("Failed to get parent path for {}", *pathResult);
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_ResourceBundle = std::move(*bundleResult);
                    m_Scene = std::move(*sceneResult);

                    if(!CreateView(*parentPathResult, m_ResourceBundle, *m_Scene))
                    {
                        MLG_ERROR("Failed to create view");
                        m_Stage = Stage::Failed;
                    }
                    else
                    {
                        m_Stage = Stage::CreatingView;
                    }
                }

                m_RsrcBundleLoadTask.reset();
            }

            break;
        case Stage::CreatingView:
            if(m_CreateViewTask->IsRunning())
            {
                m_CreateViewTask->Update();
            }
            else
            {
                if(auto viewResult = m_CreateViewTask->Take(); !viewResult)
                {
                    MLG_ERROR("Failed to take view from creation task");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Level = std::unique_ptr<Level>(
                        new Level(std::move(m_Scene), std::move(*viewResult)));
                    m_Stage = Stage::Succeeded;
                }

                m_CreateViewTask.reset();
            }
            break;

        case Stage::Failed:
            MLG_ERROR("Task failed");
            [[fallthrough]];
        case Stage::Succeeded:
            m_Timer.Stop();
            MLG_DEBUG("Level loaded in {} ms", m_Timer.GetElapsedSeconds() * 1000);
            SetComplete();
            break;
    }
}

Result<FilePath>
Level::CreateTask::GetPath() const
{
    if(std::holds_alternative<BundleParams>(m_Params))
    {
        return std::get<BundleParams>(m_Params).Path;
    }

    if(std::holds_alternative<GltfParams>(m_Params))
    {
        return std::get<GltfParams>(m_Params).Path;
    }

    return Result<>::Fail;
}

Result<>
Level::CreateTask::CreateView(
    const DirectoryPath& parentPath, const ResourceBundle& resourceBundle, Scene& scene)
{
    MLG_DEBUG("Creating view...");

    auto createResult = View::Create(*m_System, parentPath, resourceBundle, scene);

    MLG_CHECK(createResult, "Failed to create view");

    m_CreateViewTask = std::move(*createResult);

    return Result<>::Ok;
}