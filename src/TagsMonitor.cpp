#include "../include/TagsMonitor.hpp"

#include <cstdint>
#include <format>
#include <string>

#define TAG2BIT(tag) (1 << (tag - 1))

TagsMonitor::TagsMonitor() : tags(1), hist(1)
{
    std::string workspaceName = this->getWorkspaceName(1);
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + workspaceName);
    // HyprlandAPI::invokeHyprctlCommand("dispatch",
    // std::format("moveworkspacetomonitor {} {}", workspaceName,
    // this->monitorId));
}

std::string TagsMonitor::getWorkspaceName(uint16_t tag) const
{
    return std::format("{}", tag);
}

void TagsMonitor::gotoTag(uint16_t tag)
{
    if (!isValidTag(tag) || this->isOnlyTag(tag))
    {
        return;
    }

    // deactivate all borrowed tags
    uint16_t savedTags = this->tags;
    for (uint16_t t = 1; t <= 9; ++t)
    {
        this->deactivateTag(t);
    }

    // save history
    this->hist = savedTags;
    this->histMainTag = this->mainTag;
    this->tags = TAG2BIT(tag);
    this->mainTag = tag;
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + this->getWorkspaceName(tag));
}

// TODO
void TagsMonitor::flipTag(uint16_t tag)
{
    if (!isValidTag(tag) || this->isOnlyTag(tag))
    {
        return;
    }

    // deactivate all borrowed tags
    uint16_t savedTags = this->tags;
    for (uint16_t t = 1; t <= 9; ++t)
    {
        this->deactivateTag(t);
    }

    // save history
    this->hist = savedTags;
    this->histMainTag = this->mainTag;
    this->tags = TAG2BIT(tag);
    this->mainTag = tag;

    // get current workspace & save it
    // get monitor of desired workspace
    // If current monitor != monitor of desired workspace && desired workspace in
    // second monitor dispatch workspace to second monitor

    PHLWORKSPACE otherWorkspace = g_pCompositor->getWorkspaceByName(this->getWorkspaceName(tag));
    if (otherWorkspace)
    {
        auto otherMonitor = g_pCompositor->getMonitorFromID(otherWorkspace->m_iMonitorID);
        auto otherMonitorFocusedWorkspace = otherMonitor->activeWorkspace->m_szName;
        if (otherWorkspace->m_iMonitorID != GET_CURRENT_MONITOR()->ID &&
            otherMonitorFocusedWorkspace == otherWorkspace->m_szName)
        {
            HyprlandAPI::invokeHyprctlCommand(
                "dispatch",
                std::format("swapactiveworkspaces {} {}", GET_CURRENT_MONITOR()->ID, otherWorkspace->m_iMonitorID));
        }
        else
        {
            HyprlandAPI::invokeHyprctlCommand("dispatch",
                                              std::format("focusworkspaceoncurrentmonitor {}", getWorkspaceName(tag)));
        }
    }
    else
    {
        HyprlandAPI::invokeHyprctlCommand("dispatch",
                                          std::format("focusworkspaceoncurrentmonitor {}", getWorkspaceName(tag)));
    }

    // Move old workspace to second monitor
}

bool TagsMonitor::activateTag(uint16_t tag)
{
    if (!isValidTag(tag))
    {
        return false;
    }

    // tag is already active
    if (tags & TAG2BIT(tag))
    {
        return false;
    }

    PHLWORKSPACE currentWorkspace = GET_ACTIVE_WORKSPACE();
    std::unordered_set<CWindow *> borrowedWindows = getWindowsOnWorkspace(this->getWorkspaceName(tag));

    // save borrowed windows for this tag
    this->borrowedTags[tag] = borrowedWindows;
    // move them over to our main workspace
    for (auto w : borrowedWindows)
    {
        HyprlandAPI::invokeHyprctlCommand("dispatch", std::format("movetoworkspacesilent {},address:0x{:x}",
                                                                  currentWorkspace->m_szName, (uintptr_t)w));
    }

    tags |= TAG2BIT(tag);
    return true;
}

bool TagsMonitor::deactivateTag(uint16_t tag)
{
    if (!isValidTag(tag))
    {
        return false;
    }

    // tag is not active
    if (!(tags & TAG2BIT(tag)))
    {
        return false;
    }

    std::string borrowedWorkspaceName = this->getWorkspaceName(tag);

    // move windows to their original workspace
    for (auto &w : this->borrowedTags[tag])
    {
        HyprlandAPI::invokeHyprctlCommand(
            "dispatch", std::format("movetoworkspacesilent {},address:0x{:x}", borrowedWorkspaceName, (uintptr_t)w));
    }
    // clear the borrowed information for the tag
    this->borrowedTags.erase(tag);

    tags &= ~TAG2BIT(tag);
    return true;
}

bool TagsMonitor::toogleTag(uint16_t tag)
{
    // can't activate/deactivate the
    if (this->isOnlyTag(tag))
    {
        return false;
    }

    if (this->tags & TAG2BIT(tag))
    {
        return deactivateTag(tag);
    }
    return activateTag(tag);
}

void TagsMonitor::moveCurrentWindowToTag(uint16_t tag)
{
    if (!isValidTag(tag))
    {
        return;
    }

    PHLWORKSPACE currentWorkspace = getActiveWorkspace();
    CWindow *activeWindow = currentWorkspace->getLastFocusedWindow().get();

    if (activeWindow == nullptr)
    {
        // no window, do nothing
        return;
    }

    // remove the window from any tag it might be borrowed from
    for (auto &p : this->borrowedTags)
    {
        p.second.erase(activeWindow);
    }

    if (this->borrowedTags.contains(tag))
    {
        // the tag is borrowed, we'll keep the window
        // mark it as borrowed for the tag
        this->borrowedTags[tag].insert(activeWindow);
    }
    else
    {
        // otherwise, it belonged to the current main workspace
        // just move it
        HyprlandAPI::invokeHyprctlCommand("dispatch",
                                          std::format("movetoworkspacesilent {},address:0x{:x}",
                                                      this->getWorkspaceName(tag), (uintptr_t)activeWindow));
    }
}

void TagsMonitor::altTab()
{
    PHLWORKSPACE otherWorkspace = g_pCompositor->getWorkspaceByName(this->getWorkspaceName(this->histMainTag));
    if (otherWorkspace)
    {
        CMonitor *otherMonitor = g_pCompositor->getMonitorFromID(otherWorkspace->m_iMonitorID);
        std::string otherMonitorFocusedWorkspace = otherMonitor->activeWorkspace->m_szName;
        if (otherWorkspace->m_iMonitorID != GET_CURRENT_MONITOR()->ID &&
            otherMonitorFocusedWorkspace == otherWorkspace->m_szName)
        {
            HyprlandAPI::invokeHyprctlCommand(
                "dispatch",
                std::format("swapactiveworkspaces {} {}", GET_CURRENT_MONITOR()->ID, otherWorkspace->m_iMonitorID));
        }
    }

    uint16_t savedHist = this->hist;
    this->gotoTag(this->histMainTag);

    for (uint16_t tag = 1; savedHist > 0; ++tag, savedHist >>= 1)
    {
        if (savedHist & 1)
        {
            this->activateTag(tag);
        }
    }
}

bool TagsMonitor::isOnlyTag(uint16_t tag) const
{
    if (!isValidTag(tag))
    {
        return false;
    }

    return tags == TAG2BIT(tag);
}

void TagsMonitor::unregisterWindow(CWindow *window)
{
    if (window == nullptr)
    {
        return;
    }

    for (auto &p : this->borrowedTags)
    {
        p.second.erase(window);
    }
}

void TagsMonitor::unregisterCurrentWindow()
{
    PHLWORKSPACE currentWorkspace = getActiveWorkspace();
    CWindow *activeWindow = currentWorkspace->getLastFocusedWindow().get();

    this->unregisterWindow(activeWindow);
}

bool TagsMonitor::isValidTag(uint16_t tag)
{
    return 1 <= tag && tag <= 10;
}
