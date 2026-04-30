/*
  ==============================================================================

    TrackpadController.mm
    Created: 21 Mar 2026
    Author:  홍준영

    Objective-C++ bridge to macOS MultitouchSupport.framework (private API).
    Reads raw multitouch trackpad data and maps to a 4×4 pad grid.

  ==============================================================================
*/

#include "TrackpadController.h"
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <cmath>
#include <type_traits>

// =============================================================================
// MultitouchSupport.framework — private API declarations
// =============================================================================

typedef struct { float x, y; } MTPoint;
typedef struct { MTPoint position; MTPoint velocity; } MTVector;

typedef struct
{
    int32_t  frame;
    double   timestamp;
    int32_t  pathIndex;
    int32_t  state;
    int32_t  fingerID;
    int32_t  handID;
    MTVector normalizedVector;
    float    zTotal;        // finger pressure (~0..1)
    int32_t  _pad1;
    float    angle;
    float    majorAxis;
    float    minorAxis;
    MTVector absoluteVector;
    int32_t  _pad2;
    int32_t  _pad3;
    float    zDensity;
} MTTouch;

typedef void* MTDeviceRef;
typedef void (*MTContactFrameCallback)(MTDeviceRef device,
                                       MTTouch* touches,
                                       int32_t numTouches,
                                       double timestamp,
                                       int32_t frame);

using MTDeviceCreateDefaultFn = MTDeviceRef (*)(void);
using MTRegisterContactFrameCallbackFn = void (*)(MTDeviceRef, MTContactFrameCallback);
using MTUnregisterContactFrameCallbackFn = void (*)(MTDeviceRef, MTContactFrameCallback);
using MTDeviceStartFn = void (*)(MTDeviceRef, int);
using MTDeviceStopFn = void (*)(MTDeviceRef);
using MTDeviceReleaseFn = void (*)(MTDeviceRef);

static MTDeviceCreateDefaultFn           gMTDeviceCreateDefault = nullptr;
static MTRegisterContactFrameCallbackFn  gMTRegisterContactFrameCallback = nullptr;
static MTUnregisterContactFrameCallbackFn gMTUnregisterContactFrameCallback = nullptr;
static MTDeviceStartFn                   gMTDeviceStart = nullptr;
static MTDeviceStopFn                    gMTDeviceStop = nullptr;
static MTDeviceReleaseFn                 gMTDeviceRelease = nullptr;
static void*                             gMultitouchFrameworkHandle = nullptr;

static bool loadMultitouchSymbols()
{
    if (gMTDeviceCreateDefault != nullptr
        && gMTRegisterContactFrameCallback != nullptr
        && gMTUnregisterContactFrameCallback != nullptr
        && gMTDeviceStart != nullptr
        && gMTDeviceStop != nullptr
        && gMTDeviceRelease != nullptr)
        return true;

    if (gMultitouchFrameworkHandle == nullptr)
    {
        gMultitouchFrameworkHandle = dlopen(
            "/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport",
            RTLD_NOW);
    }

    if (gMultitouchFrameworkHandle == nullptr)
    {
        DBG("TrackpadController: failed to load MultitouchSupport.framework");
        return false;
    }

    auto loadSym = [](auto& fn, const char* name) -> bool
    {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(gMultitouchFrameworkHandle, name));
        if (fn == nullptr)
            DBG("TrackpadController: missing symbol " << name);
        return fn != nullptr;
    };

    const bool ok =
        loadSym(gMTDeviceCreateDefault, "MTDeviceCreateDefault") &&
        loadSym(gMTRegisterContactFrameCallback, "MTRegisterContactFrameCallback") &&
        loadSym(gMTUnregisterContactFrameCallback, "MTUnregisterContactFrameCallback") &&
        loadSym(gMTDeviceStart, "MTDeviceStart") &&
        loadSym(gMTDeviceStop, "MTDeviceStop") &&
        loadSym(gMTDeviceRelease, "MTDeviceRelease");

    return ok;
}

// =============================================================================
// Touch state constants
// =============================================================================

enum
{
    kMTStateNotTracking   = 0,
    kMTStateStartInRange  = 1,
    kMTStateHoverInRange  = 2,
    kMTStateMakeTouch     = 3,
    kMTStateTouching      = 4,
    kMTStateBreakTouch    = 5,
    kMTStateLingerInRange = 6,
    kMTStateOutOfRange    = 7
};

// =============================================================================
// Pimpl implementation
// =============================================================================

struct TrackpadController::Impl
{
    TrackpadController* owner = nullptr;
    MTDeviceRef device        = nullptr;
    bool running              = false;

    // Finger → pad tracking (prevents re-triggering while held)
    std::map<int32_t, int> fingerPadMap;
    juce::SpinLock         lock;

    // ---- Grid mapping ----
    // Trackpad normalized coords: (0,0) = bottom-left, (1,1) = top-right.
    // We want the app launchpad's top-left 4x4 block, but that block lives
    // inside an 8x8 pad matrix, so indices are row*8+col rather than a dense
    // 0..15 sequence.
    //
    // The goal is to make the MacBook trackpad behave like the app's 4x4
    // launchpad grid whose origin is the top-left pad.
    //
    // MultitouchSupport's normalized position is already expressed over the
    // full device range: (0,0)=bottom-left, (1,1)=top-right. So the correct
    // mapping here is a direct 4-way quantisation on each axis. Hard-coded
    // "usable range" trimming shifts the row boundaries downward and makes the
    // upper rows harder to hit.

    static int coordToPadIdx(float x, float y)
    {
        const float nx = juce::jlimit(0.0f, 1.0f, x);
        const float ny = juce::jlimit(0.0f, 1.0f, y);

        const int col = juce::jlimit(0, 3, (int) std::floor(nx * 4.0f));
        const int row = juce::jlimit(0, 3, 3 - (int) std::floor(ny * 4.0f));
        return row * 8 + col;
    }

    // ---- C callback (static — no user-data param available) ----
    static Impl* sInstance;

    static void contactFrameCallback(MTDeviceRef /*device*/,
                                     MTTouch* touches,
                                     int32_t numTouches,
                                     double /*timestamp*/,
                                     int32_t /*frame*/)
    {
        if (sInstance == nullptr) return;
        sInstance->processFrame(touches, numTouches);
    }

    // ---- Process one frame of touches (runs on MT private thread) ----
    void processFrame(MTTouch* touches, int32_t numTouches)
    {
        struct PadEvent { int padIdx; float velocity; bool noteOn; };
        std::vector<PadEvent> events;

        {
            juce::SpinLock::ScopedLockType sl(lock);

            for (int32_t i = 0; i < numTouches; ++i)
            {
                const auto& t = touches[i];
                const float x = t.normalizedVector.position.x;
                const float y = t.normalizedVector.position.y;
                const int   padIdx   = coordToPadIdx(x, y);
                const float velocity = juce::jlimit(0.1f, 1.0f, t.zTotal);

                if (t.state == kMTStateMakeTouch)
                {
                    // Finger just pressed
                    DBG("Trackpad touch: raw(" << x << ", " << y
                        << juce::String::fromUTF8(") \xe2\x86\x92 pad ") << padIdx
                        << "  col=" << (padIdx % 4) << " row=" << (padIdx / 4));
                    fingerPadMap[t.fingerID] = padIdx;
                    events.push_back({ padIdx, velocity, true });
                }
                else if (t.state == kMTStateTouching)
                {
                    // Finger sliding — if it crossed into a different pad, retrigger
                    auto it = fingerPadMap.find(t.fingerID);
                    if (it != fingerPadMap.end() && it->second != padIdx)
                    {
                        events.push_back({ it->second, 0.0f, false });   // release old
                        it->second = padIdx;
                        events.push_back({ padIdx, velocity, true });    // trigger new
                    }
                }
                else if (t.state == kMTStateBreakTouch)
                {
                    // Finger lifted
                    auto it = fingerPadMap.find(t.fingerID);
                    if (it != fingerPadMap.end())
                    {
                        events.push_back({ it->second, 0.0f, false });
                        fingerPadMap.erase(it);
                    }
                }
            }
        }

        // Post to JUCE message thread
        if (!events.empty() && owner != nullptr && owner->onPadEvent)
        {
            auto callback = owner->onPadEvent;
            juce::MessageManager::callAsync([events = std::move(events), callback]()
            {
                for (const auto& ev : events)
                    callback(ev.padIdx, ev.velocity, ev.noteOn);
            });
        }
    }
};

TrackpadController::Impl* TrackpadController::Impl::sInstance = nullptr;

// =============================================================================
// Public interface
// =============================================================================

TrackpadController::TrackpadController()
    : pimpl(std::make_unique<Impl>())
{
    pimpl->owner = this;
}

TrackpadController::~TrackpadController()
{
    stop();
}

void TrackpadController::start()
{
    if (pimpl->running) return;

    if (!loadMultitouchSymbols())
        return;

    pimpl->device = gMTDeviceCreateDefault();
    if (pimpl->device == nullptr)
    {
        DBG("TrackpadController: MTDeviceCreateDefault() returned nullptr");
        return;
    }

    Impl::sInstance = pimpl.get();
    gMTRegisterContactFrameCallback(pimpl->device, Impl::contactFrameCallback);
    gMTDeviceStart(pimpl->device, 0);
    pimpl->running = true;
    DBG("TrackpadController: started (4x4 grid)");
}

void TrackpadController::stop()
{
    if (!pimpl->running) return;

    gMTDeviceStop(pimpl->device);
    gMTUnregisterContactFrameCallback(pimpl->device, Impl::contactFrameCallback);
    gMTDeviceRelease(pimpl->device);
    pimpl->device = nullptr;
    pimpl->running = false;

    if (Impl::sInstance == pimpl.get())
        Impl::sInstance = nullptr;

    juce::SpinLock::ScopedLockType sl(pimpl->lock);
    pimpl->fingerPadMap.clear();
    DBG("TrackpadController: stopped");
}

bool TrackpadController::isRunning() const
{
    return pimpl->running;
}
