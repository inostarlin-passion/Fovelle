#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int NativeDragSkip = 77;
constexpr int DragSteps = 32;
constexpr int EventDelayMilliseconds = 12;
constexpr int TransitionTimeoutMilliseconds = 8000;

struct Options
{
    std::string appPath;
    std::string imagePath;
    bool checkAccess{ false };
    bool requestAccess{ false };
    bool showHelp{ false };
};

struct AccessState
{
    bool postEvent{ false };
    bool accessibility{ false };
};

struct WindowInfo
{
    CGWindowID id{ kCGNullWindowID };
    pid_t pid{ 0 };
    CGRect bounds{ };
};

struct ViewportSample
{
    std::string phase;
    double zoom{ 0.0 };
    int viewportHeight{ 0 };
    int value{ 0 };
    int minimum{ 0 };
    int maximum{ 0 };
};

struct Result
{
    bool passed{ false };
    bool normalBottom{ false };
    bool fullScreenEntryBottom{ false };
    bool fullScreenDragBottom{ false };
    bool exitBottom{ false };
    bool noOriginReset{ false };
    int dragEvents{ 0 };
    std::string logPath;
};

std::string cfStringToUtf8(CFStringRef value)
{
    if (!value)
        return { };

    const CFIndex length = CFStringGetLength(value);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<size_t>(capacity), '\0');
    if (!CFStringGetCString(value, result.data(), capacity, kCFStringEncodingUTF8))
        return { };

    result.resize(std::strlen(result.c_str()));
    return result;
}

bool getInt32(CFDictionaryRef dictionary, CFStringRef key, int32_t &result)
{
    const CFTypeRef rawValue = CFDictionaryGetValue(dictionary, key);
    if (!rawValue || CFGetTypeID(rawValue) != CFNumberGetTypeID())
        return false;

    return CFNumberGetValue(static_cast<CFNumberRef>(rawValue), kCFNumberSInt32Type, &result);
}

std::optional<CGRect> getWindowBounds(CFDictionaryRef dictionary)
{
    const CFTypeRef rawValue = CFDictionaryGetValue(dictionary, kCGWindowBounds);
    if (!rawValue || CFGetTypeID(rawValue) != CFDictionaryGetTypeID())
        return std::nullopt;

    CGRect bounds{ };
    if (!CGRectMakeWithDictionaryRepresentation(static_cast<CFDictionaryRef>(rawValue), &bounds))
        return std::nullopt;

    return bounds;
}

std::optional<WindowInfo> findApplicationWindow(pid_t pid)
{
    const CGWindowListOption options =
            kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements;
    CFArrayRef windows = CGWindowListCopyWindowInfo(options, kCGNullWindowID);
    if (!windows)
        return std::nullopt;

    std::optional<WindowInfo> best;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < count; ++index) {
        const CFTypeRef rawWindow = CFArrayGetValueAtIndex(windows, index);
        if (!rawWindow || CFGetTypeID(rawWindow) != CFDictionaryGetTypeID())
            continue;

        const auto dictionary = static_cast<CFDictionaryRef>(rawWindow);
        int32_t ownerPid = 0;
        int32_t layer = 0;
        if (!getInt32(dictionary, kCGWindowOwnerPID, ownerPid)
            || ownerPid != static_cast<int32_t>(pid) || !getInt32(dictionary, kCGWindowLayer, layer)
            || layer != 0) {
            continue;
        }

        const auto bounds = getWindowBounds(dictionary);
        if (!bounds.has_value() || CGRectIsEmpty(bounds.value()))
            continue;

        const double area = CGRectGetWidth(bounds.value()) * CGRectGetHeight(bounds.value());
        const double bestArea = best.has_value()
                ? CGRectGetWidth(best->bounds) * CGRectGetHeight(best->bounds)
                : 0.0;
        if (!best.has_value() || area > bestArea) {
            int32_t windowId = 0;
            if (!getInt32(dictionary, kCGWindowNumber, windowId))
                continue;

            best = WindowInfo{
                static_cast<CGWindowID>(windowId),
                pid,
                bounds.value(),
            };
        }
    }

    CFRelease(windows);
    return best;
}

bool sameWindowSize(const CGRect lhs, const CGRect rhs)
{
    return std::abs(CGRectGetWidth(lhs) - CGRectGetWidth(rhs)) < 10.0
            && std::abs(CGRectGetHeight(lhs) - CGRectGetHeight(rhs)) < 10.0
            && std::abs(CGRectGetMinX(lhs) - CGRectGetMinX(rhs)) < 10.0
            && std::abs(CGRectGetMinY(lhs) - CGRectGetMinY(rhs)) < 10.0;
}

bool isAccessibilityTrusted(const bool prompt)
{
    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *values[] = { prompt ? kCFBooleanTrue : kCFBooleanFalse };
    CFDictionaryRef options =
            CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                               &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    return trusted;
}

AccessState getAccessState(const bool prompt)
{
    AccessState state;
    state.postEvent = CGPreflightPostEventAccess();
    state.accessibility = isAccessibilityTrusted(prompt);
    return state;
}

void printAccessState(const AccessState state)
{
    std::cout << "NATIVE_DRAG_ACCESS post_event=" << (state.postEvent ? "true" : "false")
              << " accessibility=" << (state.accessibility ? "true" : "false") << '\n';
}

bool waitForAccess(const bool request)
{
    AccessState state = getAccessState(false);
    if (state.postEvent && state.accessibility) {
        printAccessState(state);
        return true;
    }

    if (!request) {
        printAccessState(state);
        std::cerr << "Enable Post Event and Accessibility access for this helper, then "
                     "rerun with --request-access if macOS has not prompted yet.\n";
        return false;
    }

    if (!state.postEvent)
        CGRequestPostEventAccess();
    if (!state.accessibility)
        isAccessibilityTrusted(true);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        state = getAccessState(false);
        if (state.postEvent && state.accessibility) {
            printAccessState(state);
            return true;
        }
    } while (std::chrono::steady_clock::now() < deadline);

    printAccessState(state);
    std::cerr << "The requested macOS permissions were not granted before the timeout.\n";
    return false;
}

std::string readFile(const std::string &path)
{
    std::ifstream stream(path);
    if (!stream)
        return { };

    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<ViewportSample> parseViewportSamples(const std::string &contents)
{
    static const std::regex pattern(
            R"(FOVELLE_VIEW\s+phase=\s*([^\s]+).*?zoom=\s*([-+0-9.eE]+).*?vbar=\s*(-?\d+)\s+(-?\d+)\s+(-?\d+))");
    static const std::regex viewportPattern(R"(viewportRect=\s*QRect\([^)]*\s+\d+x(\d+)\))");
    std::vector<ViewportSample> samples;
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, pattern))
            continue;

        std::smatch viewportMatch;
        const int viewportHeight = std::regex_search(line, viewportMatch, viewportPattern)
                ? std::stoi(viewportMatch[1].str())
                : 0;
        samples.push_back(ViewportSample{
                match[1].str(),
                std::stod(match[2].str()),
                viewportHeight,
                std::stoi(match[3].str()),
                std::stoi(match[4].str()),
                std::stoi(match[5].str()),
        });
    }
    return samples;
}

template <typename Predicate>
bool waitForLog(const std::string &path, Predicate predicate, const int timeoutMilliseconds,
                std::vector<ViewportSample> *result = nullptr)
{
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    std::vector<ViewportSample> samples;
    do {
        samples = parseViewportSamples(readFile(path));
        if (predicate(samples)) {
            if (result)
                *result = samples;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);

    if (result)
        *result = samples;
    return false;
}

bool isBottom(const ViewportSample &sample)
{
    return sample.maximum > sample.minimum && sample.value >= sample.maximum - 1;
}

bool latestBottomSampleAfter(const std::vector<ViewportSample> &samples, const size_t start,
                             const int minimumViewportHeight, const int maximumViewportHeight = 0)
{
    if (samples.size() <= start)
        return false;
    const int viewportHeight = samples.back().viewportHeight;
    if (viewportHeight < minimumViewportHeight
        || (maximumViewportHeight > 0 && viewportHeight > maximumViewportHeight))
        return false;
    return isBottom(samples.back());
}

int latestViewportHeight(const std::vector<ViewportSample> &samples)
{
    for (auto iterator = samples.crbegin(); iterator != samples.crend(); ++iterator) {
        if (iterator->viewportHeight > 0)
            return iterator->viewportHeight;
    }
    return 0;
}

template <typename Predicate>
bool waitForStableLog(const std::string &path, Predicate predicate, const int timeoutMilliseconds,
                      std::vector<ViewportSample> *result = nullptr)
{
    constexpr int StablePolls = 4;
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    std::vector<ViewportSample> samples;
    size_t previousSize = 0;
    int stablePollCount = 0;
    do {
        samples = parseViewportSamples(readFile(path));
        const bool condition = predicate(samples);
        if (condition && samples.size() == previousSize)
            ++stablePollCount;
        else
            stablePollCount = 0;

        if (condition && stablePollCount >= StablePolls) {
            if (result)
                *result = samples;
            return true;
        }

        previousSize = samples.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);

    if (result)
        *result = samples;
    return false;
}

bool hasOriginResetAfter(const std::vector<ViewportSample> &samples, const size_t start)
{
    return std::any_of(samples.cbegin() + std::min(start, samples.size()), samples.cend(),
                       [](const ViewportSample &sample) {
                           return sample.maximum > sample.minimum
                                   && sample.value <= sample.minimum + 1;
                       });
}

bool postMouseEvent(CGEventType type, const CGPoint point, const int clickState = 1)
{
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source)
        return false;

    CGEventRef event = CGEventCreateMouseEvent(source, type, point, kCGMouseButtonLeft);
    if (!event) {
        CFRelease(source);
        return false;
    }

    CGEventSetIntegerValueField(event, kCGMouseEventClickState, clickState);
    // The reproduction must enter the same system-wide HID queue as a real
    // mouse.  Do not replace this with AX actions, QtTest events, or a widget
    // method call.
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    CFRelease(source);
    return true;
}

bool postScrollEvent(const CGPoint point, const int delta)
{
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source)
        return false;

    CGEventRef event = CGEventCreateScrollWheelEvent(source, kCGScrollEventUnitLine, 1, delta);
    if (!event) {
        CFRelease(source);
        return false;
    }

    CGEventSetLocation(event, point);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    CFRelease(source);
    return true;
}

bool postDoubleClick(const CGPoint point)
{
    for (int click = 1; click <= 2; ++click) {
        if (!postMouseEvent(kCGEventLeftMouseDown, point, click)
            || !postMouseEvent(kCGEventLeftMouseUp, point, click)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(70));
    }
    return true;
}

CGPoint pointInWindow(const CGRect bounds, const double x, const double y)
{
    return CGPointMake(CGRectGetMinX(bounds) + CGRectGetWidth(bounds) * x,
                       CGRectGetMinY(bounds) + CGRectGetHeight(bounds) * y);
}

bool postFixedDrag(const CGRect bounds, int &dragEvents)
{
    // One normalized path is used in both normal and full-screen window
    // geometries.  Only its screen-space origin/scale changes with the window.
    const CGPoint start = pointInWindow(bounds, 0.50, 0.94);
    const CGPoint end = pointInWindow(bounds, 0.50, 0.04);
    if (!postMouseEvent(kCGEventLeftMouseDown, start))
        return false;

    for (int step = 1; step <= DragSteps; ++step) {
        const double fraction = static_cast<double>(step) / DragSteps;
        const CGPoint point = CGPointMake(start.x + (end.x - start.x) * fraction,
                                          start.y + (end.y - start.y) * fraction);
        if (!postMouseEvent(kCGEventLeftMouseDragged, point))
            return false;
        ++dragEvents;
        std::this_thread::sleep_for(std::chrono::milliseconds(EventDelayMilliseconds));
    }

    return postMouseEvent(kCGEventLeftMouseUp, end);
}

std::optional<WindowInfo> waitForApplicationWindow(const pid_t pid, const int timeoutMilliseconds)
{
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    do {
        const auto window = findApplicationWindow(pid);
        if (window.has_value())
            return window;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);
    return std::nullopt;
}

std::optional<WindowInfo> waitForWindowGeometryChange(const pid_t pid, const CGRect reference,
                                                      const int timeoutMilliseconds)
{
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    do {
        const auto window = findApplicationWindow(pid);
        if (window.has_value() && !sameWindowSize(window->bounds, reference))
            return window;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);
    return std::nullopt;
}

std::optional<WindowInfo> waitForStableWindowGeometry(const pid_t pid, const CGRect reference,
                                                      const int timeoutMilliseconds)
{
    constexpr int StablePolls = 3;
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    int stablePollCount = 0;
    do {
        const auto window = findApplicationWindow(pid);
        if (window.has_value() && sameWindowSize(window->bounds, reference)) {
            ++stablePollCount;
            if (stablePollCount >= StablePolls)
                return window;
        } else {
            stablePollCount = 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);
    return std::nullopt;
}

class LaunchedApplication
{
public:
    ~LaunchedApplication()
    {
        if (!task || !task.isRunning)
            return;

        [task terminate];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (task.isRunning && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    NSTask *task{ nil };
    std::string logPath;
};

std::unique_ptr<LaunchedApplication> launchApplication(const Options &options)
{
    std::string executablePath = options.appPath;
    if (executablePath.size() >= 4
        && executablePath.compare(executablePath.size() - 4, 4, ".app") == 0) {
        executablePath += "/Contents/MacOS/Fovelle";
    }
    if (access(executablePath.c_str(), X_OK) != 0) {
        std::cerr << "Cannot resolve the app executable from " << options.appPath << '\n';
        return nullptr;
    }

    char logTemplate[] = "/tmp/fovelle-native-drag-XXXXXX.log";
    const int logFileDescriptor = mkstemps(logTemplate, 4);
    if (logFileDescriptor < 0) {
        std::cerr << "Cannot create the native drag diagnostic log.\n";
        return nullptr;
    }

    NSFileHandle *logFile = [[NSFileHandle alloc] initWithFileDescriptor:logFileDescriptor
                                                          closeOnDealloc:YES];
    NSTask *task = [[NSTask alloc] init];
    task.launchPath = [NSString stringWithUTF8String:executablePath.c_str()];
    task.arguments = @[ [NSString stringWithUTF8String:options.imagePath.c_str()] ];
    NSMutableDictionary *environment = [[[NSProcessInfo processInfo] environment] mutableCopy];
    [environment setObject:@"1" forKey:@"FOVELLE_DIAGNOSTIC_LOG"];
    [environment setObject:@"1" forKey:@"FOVELLE_DISABLE_AUTO_UPDATE_CHECK"];
    task.environment = environment;
    task.standardOutput = logFile;
    task.standardError = logFile;

    @try {
        [task launch];
    } @catch (NSException *exception) {
        std::cerr << "Cannot launch Fovelle: " << [[exception reason] UTF8String] << '\n';
        return nullptr;
    }

    auto application = std::make_unique<LaunchedApplication>();
    application->task = task;
    application->logPath = logTemplate;
    std::cout << "NATIVE_DRAG_LOG " << application->logPath << '\n';

    NSRunningApplication *running =
            [NSRunningApplication runningApplicationWithProcessIdentifier:task.processIdentifier];
    [running activateWithOptions:NSApplicationActivateAllWindows];
    return application;
}

bool fileExists(const std::string &path)
{
    struct stat fileStatus{ };
    return stat(path.c_str(), &fileStatus) == 0 && S_ISREG(fileStatus.st_mode);
}

int runReproduction(const Options &options)
{
    if (!fileExists(options.imagePath)) {
        std::cerr << "SKIP: native drag fixture is not available: " << options.imagePath << '\n';
        return NativeDragSkip;
    }

    auto application = launchApplication(options);
    if (!application)
        return 1;

    const pid_t pid = application->task.processIdentifier;
    const auto normalWindow = waitForApplicationWindow(pid, 10000);
    if (!normalWindow.has_value()) {
        std::cerr << "The launched Fovelle process did not expose an on-screen window.\n";
        return 1;
    }

    const CGPoint normalInteractionPoint = pointInWindow(normalWindow->bounds, 0.50, 0.50);
    constexpr int NativeZoomScrollEvents = 3;
    for (int step = 0; step < NativeZoomScrollEvents; ++step) {
        if (!postScrollEvent(normalInteractionPoint, 1)) {
            std::cerr << "Failed to post the native zoom scroll event.\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    std::vector<ViewportSample> samples;
    const bool zoomed = waitForLog(
            application->logPath,
            [](const std::vector<ViewportSample> &current) {
                return std::any_of(current.cbegin(), current.cend(),
                                   [](const ViewportSample &sample) {
                                       return sample.maximum > sample.minimum;
                                   });
            },
            5000, &samples);
    if (!zoomed) {
        std::cerr << "The native scroll sequence did not produce a zoomed, scrollable image.\n";
        return 1;
    }

    Result result;
    result.logPath = application->logPath;
    const size_t normalDragStart = samples.size();
    if (!postFixedDrag(normalWindow->bounds, result.dragEvents)) {
        std::cerr << "Failed to post the normal-window native drag sequence.\n";
        return 1;
    }
    result.normalBottom = waitForStableLog(
            application->logPath,
            [normalDragStart](const std::vector<ViewportSample> &current) {
                return latestBottomSampleAfter(current, normalDragStart, 0);
            },
            4000, &samples);
    if (!result.normalBottom) {
        std::cerr << "The normal-window native drag did not reach the vertical maximum.\n";
        return 1;
    }

    const int normalViewportHeight = latestViewportHeight(samples);
    if (normalViewportHeight <= 0) {
        std::cerr << "The native diagnostic log did not contain a viewport height.\n";
        return 1;
    }

    const size_t entryStart = samples.size();
    if (!postDoubleClick(normalInteractionPoint)) {
        std::cerr << "Failed to post the native full-screen entry click sequence.\n";
        return 1;
    }
    const auto fullScreenWindow =
            waitForWindowGeometryChange(pid, normalWindow->bounds, TransitionTimeoutMilliseconds);
    if (!fullScreenWindow.has_value()) {
        std::cerr << "The native click sequence did not produce a changed full-screen window.\n";
        return 1;
    }
    const auto stableFullScreenWindow = waitForStableWindowGeometry(pid, fullScreenWindow->bounds,
                                                                    TransitionTimeoutMilliseconds);
    if (!stableFullScreenWindow.has_value()) {
        std::cerr << "The native full-screen window geometry did not settle.\n";
        return 1;
    }
    const int fullScreenViewportHeightFloor =
            CGRectGetHeight(fullScreenWindow->bounds) > CGRectGetHeight(normalWindow->bounds) + 20.0
            ? normalViewportHeight + 20
            : 0;

    result.fullScreenEntryBottom = waitForStableLog(
            application->logPath,
            [entryStart,
             fullScreenViewportHeightFloor](const std::vector<ViewportSample> &current) {
                return latestBottomSampleAfter(current, entryStart, fullScreenViewportHeightFloor);
            },
            TransitionTimeoutMilliseconds, &samples);
    if (!result.fullScreenEntryBottom) {
        std::cerr << "The vertical maximum was not preserved on native full-screen entry.\n";
        return 1;
    }

    const size_t fullScreenDragStart = samples.size();
    if (!postFixedDrag(fullScreenWindow->bounds, result.dragEvents)) {
        std::cerr << "Failed to post the full-screen native drag sequence.\n";
        return 1;
    }
    result.fullScreenDragBottom = waitForStableLog(
            application->logPath,
            [fullScreenDragStart,
             fullScreenViewportHeightFloor](const std::vector<ViewportSample> &current) {
                return latestBottomSampleAfter(current, fullScreenDragStart,
                                               fullScreenViewportHeightFloor);
            },
            4000, &samples);
    if (!result.fullScreenDragBottom) {
        std::cerr << "The full-screen native drag did not reach the vertical maximum.\n";
        return 1;
    }

    const size_t exitStart = samples.size();
    const CGPoint fullScreenInteractionPoint = pointInWindow(fullScreenWindow->bounds, 0.50, 0.50);
    if (!postDoubleClick(fullScreenInteractionPoint)) {
        std::cerr << "Failed to post the native full-screen exit click sequence.\n";
        return 1;
    }

    result.exitBottom = waitForStableLog(
            application->logPath,
            [exitStart, normalViewportHeight](const std::vector<ViewportSample> &current) {
                return latestBottomSampleAfter(current, exitStart, 0, normalViewportHeight + 20);
            },
            TransitionTimeoutMilliseconds, &samples);
    const auto finalWindow =
            waitForStableWindowGeometry(pid, normalWindow->bounds, TransitionTimeoutMilliseconds);
    const bool returnedToNormalGeometry = finalWindow.has_value();
    result.noOriginReset = result.exitBottom && !hasOriginResetAfter(samples, exitStart);
    result.passed = result.normalBottom && result.fullScreenEntryBottom
            && result.fullScreenDragBottom && result.exitBottom && result.noOriginReset
            && returnedToNormalGeometry;

    std::cout << "NATIVE_DRAG_RESULT passed=" << (result.passed ? "true" : "false")
              << " normal_bottom=" << (result.normalBottom ? "true" : "false")
              << " fullscreen_entry_bottom=" << (result.fullScreenEntryBottom ? "true" : "false")
              << " fullscreen_drag_bottom=" << (result.fullScreenDragBottom ? "true" : "false")
              << " exit_bottom=" << (result.exitBottom ? "true" : "false")
              << " no_origin_reset=" << (result.noOriginReset ? "true" : "false")
              << " returned_to_normal_geometry=" << (returnedToNormalGeometry ? "true" : "false")
              << " trajectory=vertical-94-to-04-steps-32"
              << " drag_events=" << result.dragEvents << '\n';

    return result.passed ? 0 : 1;
}

void printUsage(const char *program)
{
    std::cout << "Usage: " << program
              << " --app /path/to/Fovelle.app --image /path/to/image.jpeg\n"
                 "       [--check-access] [--request-access]\n"
                 "\n"
                 "Posts a fixed native HID trajectory:\n"
                 "  CGEventPost(kCGHIDEventTap, LeftMouseDown)\n"
                 "  CGEventPost(kCGHIDEventTap, LeftMouseDragged) x 32\n"
                 "  CGEventPost(kCGHIDEventTap, LeftMouseUp)\n";
}

std::optional<Options> parseOptions(const int argc, char **argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
        } else if (argument == "--check-access") {
            options.checkAccess = true;
        } else if (argument == "--request-access") {
            options.requestAccess = true;
        } else if ((argument == "--app" || argument == "--image") && index + 1 < argc) {
            const std::string value = argv[++index];
            if (argument == "--app")
                options.appPath = value;
            else
                options.imagePath = value;
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << '\n';
            return std::nullopt;
        }
    }
    return options;
}

} // namespace

int main(int argc, char **argv)
{
    @autoreleasepool {
        const auto parsedOptions = parseOptions(argc, argv);
        if (!parsedOptions.has_value()) {
            printUsage(argv[0]);
            return 2;
        }

        const Options options = parsedOptions.value();
        if (options.showHelp) {
            printUsage(argv[0]);
            return 0;
        }

        if (!waitForAccess(options.requestAccess))
            return NativeDragSkip;
        if (options.checkAccess)
            return 0;
        if (options.appPath.empty() || options.imagePath.empty()) {
            printUsage(argv[0]);
            return 2;
        }

        return runReproduction(options);
    }
}
