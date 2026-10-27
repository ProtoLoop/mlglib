#pragma once

#include "Result.h"
#include "VecMath.h"

#include <array>
#include <span>
#include <string>
#include <vector>

class CliState
{
public:
    static constexpr size_t kMaxInputChars = 1024;

    std::span<char> GetInput() { return m_Input; }

    void ClearInput();

    void AddLine(std::string line);

    void AddHistory(std::string command);

    /// Moves the history pointer back and returns the command at the new position.
    std::string_view HistoryBack();

    /// Moves the history pointer forward and returns the command at the new position.
    std::string_view HistoryForward();

    std::span<const std::string> GetLines() const { return m_Lines; }
    std::span<const std::string> GetHistory() const { return m_History; }

private:
    std::vector<std::string> m_Lines;
    std::vector<std::string> m_History;
    std::array<char, kMaxInputChars> m_Input = {};

    std::vector<std::string>::iterator m_HistoryIt = m_History.end();

    std::string m_PendingInput; // Input restored after navigating back past newest history item.
};

/// Dev UI overlay.  Pass in the renderer for access to the render target
/// so it can be rendered in the view panel.
class DevUi
{
public:
    DevUi() = default;
    ~DevUi() = default;
    DevUi(const DevUi&) = delete;
    DevUi& operator=(const DevUi&) = delete;
    DevUi(DevUi&&) = delete;
    DevUi& operator=(DevUi&&) = delete;

    Result<> Render();

    const Rect& GetViewPanelRect() const { return m_ViewPanelRect; }

    const Point2d& GetViewPanelMousePos() const { return m_ViewPanelMousePos; }

private:

    constexpr static const char* kViewPanelName = "View";
    constexpr static const char* kPerfPanelName = "Performance";
    constexpr static const char* kCliPanelName = "CLI";
    constexpr static const char* kStatusBarPanelName = "StatusBar";

    void DrawPerfPanel() const;

    void DrawViewPanel();

    void DrawCliPanel();

    void DrawStatusBarPanel() const;

    void DrawDockedEditorLayout() const;

    CliState m_CliState;

    Rect m_ViewPanelRect{{.X = 0, .Y = 0, .Width = 1, .Height = 1}};

    Point2d m_ViewPanelMousePos{0, 0};

    bool m_CliScrollToBottom{ false };
};