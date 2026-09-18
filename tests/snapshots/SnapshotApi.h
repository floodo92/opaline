// SnapshotApi.h — shared between main.cpp and story TUs.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace snap
{
    struct Story
    {
        using Factory = std::function<std::unique_ptr<juce::Component>()>;
        Factory make;
        int defaultWidth  = 0;
        int defaultHeight = 0;
    };

    inline std::unordered_map<std::string, Story>& registry()
    {
        static std::unordered_map<std::string, Story> r;
        return r;
    }

    struct Register
    {
        Register (const char* name, Story::Factory factory,
                  int w = 0, int h = 0)
        {
            registry().emplace (name, Story { std::move (factory), w, h });
        }
    };
}

#define REGISTER_COMPONENT(id, factory)                                     \
    static ::snap::Register reg_##id { #id, factory }

#define REGISTER_COMPONENT_SIZED(id, factory, w, h)                         \
    static ::snap::Register reg_##id { #id, factory, (w), (h) }