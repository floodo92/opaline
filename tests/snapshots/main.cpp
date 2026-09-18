// snapshot_main.cpp
//
// Headless harness: renders any registered juce::Component into a PNG.
// Wired via CMake (ENABLE_SNAPSHOT_TESTS=ON) — see CMakeLists.txt.
//
// Invocation:
//   ./snapshot --list                      # print registered stories, one per line
//   ./snapshot <name> <w> <h> <out.png>    # render a story
//
// Determinism levers:
//   - SoftwareImageType{} forces the same rasterizer on every OS.
//   - JUCE_DISABLE_COREGRAPHICS_FONT_SMOOTHING=1 kills macOS subpixel AA.
//   - runDispatchLoopUntil(16) lets ctor-scheduled layout/timers settle.

#include "SnapshotApi.h"

#include <cstdlib>
#include <iostream>

namespace snap
{
    inline namespace renderHelpers
    {
    int renderToFile (const std::string& name, int w, int h,
                             const juce::File& out)
    {
        auto it = registry().find (name);
        if (it == registry().end())
        {
            std::cerr << "unknown component: " << name << "\n";
            std::cerr << "run with --list to see registered stories\n";
            return 2;
        }

        const auto& story = it->second;
        if (w <= 0) w = story.defaultWidth  > 0 ? story.defaultWidth  : 400;
        if (h <= 0) h = story.defaultHeight > 0 ? story.defaultHeight : 300;

        auto component = story.make();
        component->setSize (w, h);

        // One message-thread pump so ctors that schedule layout/timers settle.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (16);

        // Force SoftwareImageType for cross-platform byte-level reproducibility.
        juce::Image img (juce::Image::ARGB, w, h, true,
                         juce::SoftwareImageType {});
        {
            juce::Graphics g (img);
            component->paintEntireComponent (g, true);
        }

        out.getParentDirectory().createDirectory();
        out.deleteFile();

        juce::FileOutputStream fs (out);
        if (! fs.openedOk())
        {
            std::cerr << "cannot open " << out.getFullPathName() << "\n";
            return 3;
        }

        juce::PNGImageFormat png;
        return png.writeImageToStream (img, fs) ? 0 : 4;
    }

    inline void printList()
    {
        for (const auto& [name, story] : registry())
            std::cout << name << '|'
                      << story.defaultWidth  << '|'
                      << story.defaultHeight << '\n';
    }
    } // inline namespace renderHelpers
} // namespace snap

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    juce::MessageManagerLock lock;

    if (argc >= 2 && std::string (argv[1]) == "--list")
    {
        ::snap::printList();
        return 0;
    }

    if (argc < 5)
    {
        std::cerr << "usage:\n"
                     "  " << (argc > 0 ? argv[0] : "snapshot")
                  << " <name> <width> <height> <out.png>\n"
                     "  " << (argc > 0 ? argv[0] : "snapshot")
                  << " --list\n\n"
                     "registered stories:\n";
        for (const auto& [k, s] : ::snap::registry())
            std::cerr << "  " << k
                      << " (default " << s.defaultWidth << "x"
                      << s.defaultHeight << ")\n";
        return 1;
    }

    return ::snap::renderToFile (
        argv[1],
        std::atoi (argv[2]),
        std::atoi (argv[3]),
        juce::File::getCurrentWorkingDirectory().getChildFile (argv[4]));
}