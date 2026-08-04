#include "app/Application.h"
#include "core/Verbose.h"

#include <OSD.hxx>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(__linux__)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

#if defined(__linux__)
// Print a backtrace on a fatal signal, THEN chain to whatever handler was
// already installed (OCCT's, from OSD::SetSignal) so its signal→exception
// conversion still runs. Diagnostic for the drag-over-window crash.
struct sigaction g_prevSegv;
void crashBacktrace(int sig, siginfo_t* info, void* ctx) {
    const char* msg = "\n*** crash backtrace ***\n";
    write(2, msg, std::strlen(msg));
    void* frames[40];
    int n = backtrace(frames, 40);
    backtrace_symbols_fd(frames, n, 2); // async-signal-safe, fd 2 = stderr
    if (g_prevSegv.sa_flags & SA_SIGINFO) {
        if (g_prevSegv.sa_sigaction) g_prevSegv.sa_sigaction(sig, info, ctx);
    } else if (g_prevSegv.sa_handler && g_prevSegv.sa_handler != SIG_DFL &&
               g_prevSegv.sa_handler != SIG_IGN) {
        g_prevSegv.sa_handler(sig);
    } else {
        _exit(139);
    }
}
void installCrashBacktrace() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashBacktrace;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &g_prevSegv);
}
#endif

struct CliOptions {
    bool safeMode = false;
    bool wantHelp = false;
    bool verbose  = false;
    const char* logPath = "/tmp/materializr.log";
    float uiScale = 0.0f;   // desktop UI scale override; 0 = use the saved setting
};

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions o;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--safe-mode")      == 0 ||
            std::strcmp(a, "--safe-graphics")  == 0 ||
            std::strcmp(a, "--low-graphics")   == 0) {
            o.safeMode = true;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            o.wantHelp = true;
        } else if (std::strcmp(a, "--verbose") == 0 || std::strcmp(a, "-v") == 0) {
            o.verbose = true;
        } else if (std::strcmp(a, "--log") == 0 && i + 1 < argc) {
            o.verbose = true;
            o.logPath = argv[++i];
        } else if ((std::strcmp(a, "--ui-scale") == 0 ||
                    std::strcmp(a, "--scale") == 0) && i + 1 < argc) {
            o.uiScale = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(a, "--hidpi") == 0) {
            o.uiScale = 2.0f;   // shortcut for the common high-DPI case
        }
    }
    return o;
}

void printHelp() {
    std::cout <<
        "Materializr - parametric 3D CAD\n"
        "\n"
        "Usage: materializr [options]\n"
        "\n"
        "Options:\n"
        "  --safe-mode | --safe-graphics | --low-graphics\n"
        "      Bring the app up in a known-safe configuration: MSAA off,\n"
        "      mesh quality Low, default lights, autosave off, auto-open\n"
        "      last project off. The safe values are written to the settings\n"
        "      file, so subsequent normal launches stay recovered. Use this\n"
        "      if a previously-saved setting crashes the app at startup or\n"
        "      if a complex auto-opened project hangs a lower-core machine.\n"
        "\n"
        "  -v, --verbose\n"
        "      Redirect stderr to /tmp/materializr.log (truncated each run)\n"
        "      so [Resize], [Push/Pull] etc. diagnostics are captured to a\n"
        "      file that can be inspected after the session.\n"
        "\n"
        "  --log <path>\n"
        "      Implies --verbose and writes the log to <path> instead of the\n"
        "      default /tmp/materializr.log.\n"
        "\n"
        "  --ui-scale <n> | --scale <n>\n"
        "      Desktop interface scale (Linux HiDPI): 1.0 = normal, 2.0 = 2x.\n"
        "      An escape hatch when the UI is too small to read to change it in\n"
        "      Settings. Overrides the saved Appearance setting for this launch.\n"
        "\n"
        "  --hidpi\n"
        "      Shortcut for --ui-scale 2.0.\n"
        "\n"
        "  -h, --help\n"
        "      Print this help and exit.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    // Line-buffer stdout even when it's a pipe (journald, a log file). Block
    // buffering held MINUTES of prints and flushed them in one burst, giving
    // every journal line the same timestamp — which made an input-storm
    // non-bug out of an ordinary session while hiding the real event order.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    CliOptions opts = parseArgs(argc, argv);
    if (opts.wantHelp) {
        printHelp();
        return 0;
    }
    if (opts.verbose) {
        // Flip the per-op log gate so [Resize], etc. fprintf(stderr, ...)
        // calls actually emit. Without this they're no-ops.
        materializr::setVerbose(true);
        // Redirect stderr to a file so diagnostics (fprintf(stderr, ...) calls
        // scattered through the modeling ops) survive past the terminal
        // session. "w" truncates so each run starts clean. Line-buffered so a
        // crash mid-op still flushes recent traces.
        std::FILE* log = std::freopen(opts.logPath, "w", stderr);
        if (log) {
            std::setvbuf(log, nullptr, _IOLBF, 0);
            std::cout << "[verbose] stderr -> " << opts.logPath << std::endl;
            std::fprintf(stderr, "[verbose] materializr log opened\n");
        } else {
            std::cerr << "[verbose] failed to open log " << opts.logPath
                      << " (continuing with stderr to terminal)" << std::endl;
        }
    }
    // Convert OCCT internal faults (SIGSEGV/SIGFPE inside the kernel — e.g. a
    // NURBS-convert on degenerate geometry) into catchable Standard_Failure
    // exceptions, so an op's try/catch (with OCC_CATCH_SIGNALS) refuses the
    // operation instead of taking the whole app down.
    OSD::SetSignal(Standard_False);
#if defined(__linux__)
    installCrashBacktrace(); // print a stack trace before OCCT's handler runs
#endif

    try {
        materializr::Application app(opts.safeMode, opts.uiScale);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
