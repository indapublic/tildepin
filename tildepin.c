// TildePin — puts the `/~ key under Esc on an ISO keyboard.
//
// On an ISO keyboard the key under Esc is the section key (§/±, virtual
// keycode 10) and `/~ sits on the extra key left of Z (keycode 50). TildePin
// swaps the two in an event tap, so the top-left key types `/~ exactly like on
// an ANSI keyboard, in every input source.
//
// The tap only rewrites the virtual keycode of key-down and key-up events:
// modifiers, key repeat and dead keys keep working as usual, and nothing is
// typed or injected behind the user's back.
//
// Homebrew installs are started at login by `brew services`. A copy installed
// from the DMG registers the launch agent in its own bundle instead and hands
// over to it; `TildePin --unregister` removes that agent again.
//
// Every install logs to ~/Library/Logs/TildePin.log.

#include <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LOG_LIMIT (512 * 1024)               // bytes, then the log moves to TildePin.old.log
#define AGENT_LABEL "com.indapublic.tildepin"  // Contents/Library/LaunchAgents/<label>.plist

// Virtual keycodes, from HIToolbox/Events.h.
#define KEY_ISO_SECTION 10  // the key under Esc on ISO hardware (§/±)
#define KEY_GRAVE 50        // the extra key left of Z on ISO hardware (`/~)

// Every pair trades places. A keycode missing here is passed through untouched.
typedef struct { CGKeyCode from, to; } Remap;
static const Remap REMAP[] = {
    { KEY_ISO_SECTION, KEY_GRAVE },
    { KEY_GRAVE, KEY_ISO_SECTION },
};

static char        selfPath[1024];
static CFMachPortRef tap;
static bool        tracing;  // --keys: print keycodes instead of remapping

// --- Log ---------------------------------------------------------------------

// Every install writes to the same file, so a bug report can always carry
// it. Started from a terminal, TildePin keeps writing to the terminal.
static void open_log(const char *home) {
    char path[PATH_MAX], old[PATH_MAX];
    struct stat st;
    if (!home || isatty(STDERR_FILENO)) return;
    snprintf(path, sizeof path, "%s/Library/Logs/TildePin.log", home);
    snprintf(old, sizeof old, "%s/Library/Logs/TildePin.old.log", home);
    if (stat(path, &st) == 0 && st.st_size > LOG_LIMIT) rename(path, old);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    dup2(fd, STDERR_FILENO);
    close(fd);
}

static void say(const char *format, ...) {
    char stamp[24];
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(stderr, "%s ", stamp);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

// --- Remapping ---------------------------------------------------------------

// The keycode an event carries, or the keycode it should carry instead.
static CGKeyCode remap_keycode(CGKeyCode code) {
    for (size_t i = 0; i < sizeof REMAP / sizeof REMAP[0]; i++)
        if (REMAP[i].from == code) return REMAP[i].to;
    return code;
}

static CGEventRef on_event(CGEventTapProxy proxy, CGEventType type, CGEventRef ev, void *ctx) {
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (!tap) return ev;  // tests call the handler without one
        CGEventTapEnable(tap, true);
        // kCGEventTapDisabledByUserInput is routine: it arrives whenever
        // secure input starts (password fields, some terminals).
        if (type == kCGEventTapDisabledByTimeout && !tracing) say("event tap re-enabled after a timeout");
        return ev;
    }

    CGKeyCode code = (CGKeyCode)CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode);
    CGKeyCode to = remap_keycode(code);

    if (tracing) {
        printf("keycode %3d%s\n", code, to != code ? "  (swapped)" : "");
        fflush(stdout);
        return ev;
    }
    if (to != code) CGEventSetIntegerValueField(ev, kCGKeyboardEventKeycode, to);
    return ev;
}

static CFMachPortRef create_tap(void) {
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
    return CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                            kCGEventTapOptionDefault, mask, on_event, NULL);
}

// --- Starting at login -------------------------------------------------------

enum { AGENT_NOT_REGISTERED, AGENT_ENABLED, AGENT_REQUIRES_APPROVAL, AGENT_NOT_FOUND };

// The SMAppService for the launch agent in this bundle, or nil before macOS 13.
// ServiceManagement is loaded on demand, so the resident process never maps it.
static id login_agent(void) {
    if (!dlopen("/System/Library/Frameworks/ServiceManagement.framework/ServiceManagement", RTLD_LAZY))
        return nil;
    Class cls = objc_getClass("SMAppService");
    if (!cls) return nil;
    return ((id (*)(Class, SEL, id))objc_msgSend)(
        cls, sel_registerName("agentServiceWithPlistName:"), (id)CFSTR(AGENT_LABEL ".plist"));
}

static bool agent_call(id agent, const char *selector) {
    return ((BOOL (*)(id, SEL, id *))objc_msgSend)(agent, sel_registerName(selector), NULL);
}

// For a copy the user opened by hand: makes sure the launch agent runs
// TildePin, now and at every login. Returns false when this process should
// do the work itself: before macOS 13, or after the user turned TildePin
// off in Login Items, which a manual start must not override.
static bool hand_over_to_agent(void) {
    id agent = login_agent();
    if (!agent) return false;
    long status = ((long (*)(id, SEL))objc_msgSend)(agent, sel_registerName("status"));
    if (status == AGENT_ENABLED) return true;
    if (status == AGENT_REQUIRES_APPROVAL) return false;
    if (!agent_call(agent, "registerAndReturnError:")) return false;
    say("registered the login agent");
    return true;
}

// `TildePin --unregister`: stops TildePin and removes it from Login Items.
static int unregister_agent(void) {
    id agent = login_agent();
    return agent && agent_call(agent, "unregisterAndReturnError:") ? 0 : 1;
}

// launchd names the job of an app opened from Finder (or `open`)
// application.<bundle id>.<n>; the agent runs as AGENT_LABEL, brew services
// as sh.brew.tildepin, and a build started from a terminal inherits its name.
static bool opened_from_finder(void) {
    const char *job = getenv("XPC_SERVICE_NAME"), *prefix = "application." AGENT_LABEL ".";
    return job && strncmp(job, prefix, strlen(prefix)) == 0;
}

static bool installed_by_homebrew(void) {
    char real[PATH_MAX];
    return realpath(selfPath, real) && strstr(real, "/Cellar/");
}

static void say_hello(void) {
    char version[32] = "dev", macos[32] = "?";
    size_t size = sizeof macos;
    CFTypeRef v = CFBundleGetValueForInfoDictionaryKey(CFBundleGetMainBundle(), CFSTR("CFBundleShortVersionString"));
    if (v && CFGetTypeID(v) == CFStringGetTypeID()) CFStringGetCString(v, version, sizeof version, kCFStringEncodingUTF8);
    sysctlbyname("kern.osproductversion", macos, &size, NULL, 0);
    say("TildePin %s on macOS %s (pid %d)", version, macos, getpid());
}

// --- Entry point -------------------------------------------------------------

static void run(char *const argv[]) {
    pid_t pid;
    if (posix_spawn(&pid, argv[0], NULL, NULL, argv, NULL) == 0) waitpid(pid, NULL, 0);
}

// Shows the system Accessibility prompt. Loaded on demand so that the
// ApplicationServices umbrella never sits in memory on normal runs.
static void request_access(void) {
    void *as = dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices", RTLD_LAZY);
    Boolean (*ask)(CFDictionaryRef) = as ? dlsym(as, "AXIsProcessTrustedWithOptions") : NULL;
    CFStringRef *promptKey = as ? dlsym(as, "kAXTrustedCheckOptionPrompt") : NULL;
    if (!ask || !promptKey) return;
    if (ask(NULL)) return;  // already allowed, the tap failed for another reason

    // An upgrade changes the ad-hoc signature: the old entry still looks enabled
    // in System Settings but no longer matches. Drop it so the prompt starts clean.
    // tccutil looks the app up through Launch Services, which does not know about
    // a bundle that was never opened from Finder, so register it first.
    CFBundleRef bundle = CFBundleGetMainBundle();
    CFStringRef bundleID = CFBundleGetIdentifier(bundle);
    CFURLRef bundleURL = CFBundleCopyBundleURL(bundle);
    char id[256], path[1024];
    if (bundleID && bundleURL
        && CFStringGetCString(bundleID, id, sizeof id, kCFStringEncodingUTF8)
        && CFURLGetFileSystemRepresentation(bundleURL, true, (UInt8 *)path, sizeof path)) {
        run((char *[]){ "/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                        "LaunchServices.framework/Support/lsregister", "-f", path, NULL });
        run((char *[]){ "/usr/bin/tccutil", "reset", "Accessibility", id, NULL });
    }
    if (bundleURL) CFRelease(bundleURL);

    const void *keys[] = { *promptKey }, *vals[] = { kCFBooleanTrue };
    CFDictionaryRef opts = CFDictionaryCreate(NULL, keys, vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    ask(opts);
    CFRelease(opts);
}

int main(int argc, char **argv) {
    const char *home = getenv("HOME");
    uint32_t size = sizeof selfPath;
    if (_NSGetExecutablePath(selfPath, &size) != 0) selfPath[0] = 0;

    if (argc > 1 && strcmp(argv[1], "--unregister") == 0) return unregister_agent();

    tracing = argc > 1 && strcmp(argv[1], "--keys") == 0;
    if (!tracing) {
        open_log(home);
        say_hello();
        if (opened_from_finder() && !installed_by_homebrew() && hand_over_to_agent()) return 0;
    }

    // AXIsProcessTrusted() can stay stale for the life of the process after the
    // user grants access, so the tap itself is the permission check.
    bool prompted = false;
    while (!(tap = create_tap())) {
        if (!prompted) {
            if (tracing) {
                fprintf(stderr, "TildePin needs Accessibility access to read key codes.\n");
            } else {
                say("waiting for Accessibility permission");
            }
            request_access();
            prompted = true;
        }
        sleep(3);
    }
    if (prompted && selfPath[0]) execv(selfPath, argv);  // start clean, without the prompt code loaded

    signal(SIGCHLD, SIG_IGN);  // children are reaped automatically

    if (tracing) {
        printf("Press keys to see their virtual keycodes. Ctrl-C to stop.\n"
               "TildePin swaps %d (%s) and %d (%s).\n",
               KEY_ISO_SECTION, "ISO section, under Esc",
               KEY_GRAVE, "grave, left of Z");
        fflush(stdout);
    } else {
        say("active: keycode %d (ISO section) <-> %d (grave)", KEY_ISO_SECTION, KEY_GRAVE);
    }

    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), src, kCFRunLoopCommonModes);
    CFRelease(src);  // the run loop keeps it
    CGEventTapEnable(tap, true);
    CFRunLoopRun();
    return 0;
}
