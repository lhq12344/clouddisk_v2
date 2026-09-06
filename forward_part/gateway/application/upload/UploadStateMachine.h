#pragma once

#include <string>

namespace core::application::upload
{

enum class UploadState
{
    Initiated,
    Uploading,
    Completing,
    PendingScan,
    Ready,
    Infected,
    Failed,
    Aborted,
    ReconcileRequired
};

inline std::string toString(UploadState state)
{
    switch (state)
    {
    case UploadState::Initiated:
        return "initiated";
    case UploadState::Uploading:
        return "uploading";
    case UploadState::Completing:
        return "completing";
    case UploadState::PendingScan:
        return "pending_scan";
    case UploadState::Ready:
        return "ready";
    case UploadState::Infected:
        return "infected";
    case UploadState::Failed:
        return "failed";
    case UploadState::Aborted:
        return "aborted";
    case UploadState::ReconcileRequired:
        return "reconcile_required";
    }
    return "unknown";
}

inline UploadState uploadStateFromString(const std::string &state)
{
    if (state == "initiated" || state == "init")
    {
        return UploadState::Initiated;
    }
    if (state == "uploading")
    {
        return UploadState::Uploading;
    }
    if (state == "completing")
    {
        return UploadState::Completing;
    }
    if (state == "pending_scan")
    {
        return UploadState::PendingScan;
    }
    if (state == "ready" || state == "success")
    {
        return UploadState::Ready;
    }
    if (state == "infected")
    {
        return UploadState::Infected;
    }
    if (state == "failed" || state == "scan_failed")
    {
        return UploadState::Failed;
    }
    if (state == "aborted")
    {
        return UploadState::Aborted;
    }
    if (state == "reconcile_required")
    {
        return UploadState::ReconcileRequired;
    }
    return UploadState::Failed;
}

constexpr bool canTransition(UploadState from, UploadState to)
{
    switch (from)
    {
	case UploadState::Initiated:
		return to == UploadState::Uploading || to == UploadState::Completing || to == UploadState::Aborted;
    case UploadState::Uploading:
        return to == UploadState::Completing || to == UploadState::Aborted || to == UploadState::Failed;
    case UploadState::Completing:
        return to == UploadState::PendingScan || to == UploadState::ReconcileRequired || to == UploadState::Failed;
    case UploadState::PendingScan:
        return to == UploadState::Ready || to == UploadState::Infected || to == UploadState::Failed;
    case UploadState::ReconcileRequired:
        return to == UploadState::PendingScan || to == UploadState::Failed;
    case UploadState::Ready:
    case UploadState::Infected:
    case UploadState::Failed:
    case UploadState::Aborted:
        return false;
    }
    return false;
}

} // namespace core::application::upload
