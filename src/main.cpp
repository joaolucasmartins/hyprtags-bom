// TODO just getting the ID of the monitor might not be enough.
// Need to check that the IDs are constant after plugs/unplugs in the same session
// Can maybe solve by moving the workspaces to a new monitor when the monitors change.
//
// TODO also need to take care of merging workspaces when monitors disappear
//
// TODO Use this to get focus: g_pCompositor->m_pLastWindow
//
// TODO deal with moving windows between monitors:
//     - maybe this could be a handler that checked for window movements and handled them
//     all there.

#define WLR_USE_UNSTABLE

#include <any>
#include <format>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <hyprland/src/includes.hpp>

#include <hyprland/src/Compositor.hpp>

#define private public
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#undef private

#include "../include/TagsMonitor.hpp"
#include "../include/globals.hpp"
#include "../include/utils.hpp"

#define GET_CURRENT_TAGMONITOR() g_tagsMonitors

// Each monitor has a set of active tags: monitorID -> listOfTags
static TagsMonitor *g_tagsMonitors;

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

static void tagsWorkspace(const std::string &workspace)
{
    Debug::log(LOG, HYPRTAGS ": tags-workspace {}", workspace);

    uint16_t workspaceIdx = (uint16_t)std::stoi(workspace);
    if (!TagsMonitor::isValidTag(workspaceIdx))
    {
        Debug::log(ERR, HYPRTAGS ": tags-workspace {} is invalid", workspace);
        return;
    }

    GET_CURRENT_TAGMONITOR()->gotoTag(workspaceIdx);
}

// TODO
static void flipWorkspace(const std::string &workspace)
{
    Debug::log(LOG, HYPRTAGS ": tags-workspace {}", workspace);

    uint16_t workspaceIdx = (uint16_t)std::stoi(workspace);
    if (!TagsMonitor::isValidTag(workspaceIdx))
    {
        Debug::log(ERR, HYPRTAGS ": tags-workspace {} is invalid", workspace);
        return;
    }

    GET_CURRENT_TAGMONITOR()->flipTag(workspaceIdx);
    // TODO
}

static void tagsWorkspacealttab(const std::string &ignored)
{
    Debug::log(LOG, HYPRTAGS ": tags-workspacealttab");

    GET_CURRENT_TAGMONITOR()->altTab();
}

static void tagsMovetoworkspacesilent(const std::string &workspace)
{
    Debug::log(LOG, HYPRTAGS ": tags-movetoworkspacesilent {}", workspace);

    if (workspace.rfind("special:", 0) == 0)
    {
        // special windows in special workspaces are not managed by us. Just need to make sure the window is not borrowed
        // by anyone before moving it.
        Debug::log(LOG, HYPRTAGS ": tags-movetoworkspacesilent {} is a special workspace", workspace);
        GET_CURRENT_TAGMONITOR()->unregisterCurrentWindow();
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent " + workspace);
        return;
    }

    uint16_t workspaceIdx = (uint16_t)std::stoi(workspace);
    if (!TagsMonitor::isValidTag(workspaceIdx))
    {
        Debug::log(ERR, HYPRTAGS ": tags-movetoworkspacesilent {} is invalid", workspace);
        return;
    }

    GET_CURRENT_TAGMONITOR()->moveCurrentWindowToTag(workspaceIdx);
}

static void tagsMovetoworkspace(const std::string &workspace)
{
    Debug::log(LOG, HYPRTAGS ": tags-movetoworkspace {}", workspace);

    uint16_t workspaceIdx = (uint16_t)std::stoi(workspace);
    if (!TagsMonitor::isValidTag(workspaceIdx))
    {
        Debug::log(ERR, HYPRTAGS ": tags-movetoworkspace {} is invalid", workspace);
        return;
    }

    auto tagMon = GET_CURRENT_TAGMONITOR();
    tagMon->moveCurrentWindowToTag(workspaceIdx);
    tagMon->gotoTag(workspaceIdx);
}

static void tagsToggleworkspace(const std::string &workspace)
{
    Debug::log(LOG, HYPRTAGS ": tags-toggleworkspace {}", workspace);

    uint16_t workspaceIdx = (uint16_t)std::stoi(workspace);
    if (!TagsMonitor::isValidTag(workspaceIdx))
    {
        Debug::log(ERR, HYPRTAGS ": tags-workspace {} is invalid", workspace);
        return;
    }

    GET_CURRENT_TAGMONITOR()->toogleTag(workspaceIdx);
}

/**
 * @brief Notification for user-triggered workspace changes that did not go through the plugin.
 *
 * @param workspace The workspace that changed
 */
static void onWorkspace(void *self, std::any data, HANDLE PHANDLE)
{
    const auto workspace = std::any_cast<PHLWORKSPACE>(data);
    // Debug::log(LOG, HYPRTAGS ": onWorkspace {}", workspace->m_szName);
    HyprlandAPI::addNotification(PHANDLE, "Here", CColor{0.2, 1.0, 0.2, 1.0}, 5000);

    // if the workspace is special, do nothing (we don't manage special workspaces)
    if (workspace->m_bIsSpecialWorkspace)
    {
        return;
    }

    auto tagMon = GET_CURRENT_TAGMONITOR();
    uint16_t workspaceIdx = (uint16_t)std::stoi(workspace->m_szName);
    // if the workspace is the current one, it means we were the ones that triggered the change, so do nothing
    if (tagMon->isOnlyTag(workspaceIdx))
    {
        return;
    }

    tagMon->gotoTag(workspaceIdx);
}

static void onCloseWindow(void *self, std::any data, HANDLE PHANDLE)
{
    // HyprlandAPI::addNotification(PHANDLE, HYPRTAGS ": Initialized successfully?!", CColor{0.2, 1.0, 0.2, 1.0}, 5000);
    // data is guaranteed
    const auto PWINDOW = std::any_cast<PHLWINDOW>(data);

    Debug::log(LOG, HYPRTAGS ": onCloseWindow {}", (uintptr_t)PWINDOW.get());

    GET_CURRENT_TAGMONITOR()->unregisterWindow(PWINDOW.get());
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH)
    {
        HyprlandAPI::addNotification(PHANDLE, HYPRTAGS ": Failure in initialization: Version mismatch (headers ver is not equal to running Hyprland ver)",
                                     CColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hww] Version mismatch. HASH=" + HASH + ", GIT_COMMIT_HASH=" + GIT_COMMIT_HASH);
    }

    HyprlandAPI::addDispatcher(PHANDLE, "tags-workspace", tagsWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "tags-flipworkspace", flipWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "tags-workspacealttab", tagsWorkspacealttab);
    HyprlandAPI::addDispatcher(PHANDLE, "tags-movetoworkspacesilent", tagsMovetoworkspacesilent);
    HyprlandAPI::addDispatcher(PHANDLE, "tags-movetoworkspace", tagsMovetoworkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "tags-toggleworkspace", tagsToggleworkspace);
    // so keybinds work and the errors disappear
    HyprlandAPI::reloadConfig();

    static auto P1 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "workspace", [&](void *self, SCallbackInfo &info, std::any data)
                                                          { onWorkspace(self, data, PHANDLE); });
    static auto P2 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "closeWindow", [&](void *self, SCallbackInfo &info, std::any data)
                                                          { onCloseWindow(self, data, PHANDLE); });

    // At the start only the first tag is active
    TagsMonitor *tagsMonitor = new TagsMonitor();
    g_tagsMonitors = tagsMonitor;
    // focus main screen
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:1");

    HyprlandAPI::addNotification(PHANDLE, HYPRTAGS ": Initialized successfully?!", CColor{0.2, 1.0, 0.2, 1.0}, 5000);
    return {HYPRTAGS, "Hyprland version of DWM's tag system", "JoaoCostaIFG", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    HyprlandAPI::addNotification(PHANDLE, HYPRTAGS ": Unloaded!", CColor{0.2, 1.0, 0.2, 1.0}, 5000);

    delete g_tagsMonitors; // clear();
}
