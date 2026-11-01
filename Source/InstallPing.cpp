#include "InstallPing.h"

#include <thread>

namespace
{
    const juce::String kPingFlagFileName = "opaline_install_ping_sent";
    const juce::String kEndpointBase = "https://floodo.com.ar/backend/api/";
    const juce::String kPingPath = "device-ping";
    const juce::String kPluginId = "plg_opaline";

    juce::File getFlagDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("FloodosAudioLabs");
    }

    bool hasAlreadySent()
    {
        const auto dir = getFlagDirectory();

        if (!dir.exists())
            return false;

        return dir.getChildFile(kPingFlagFileName).existsAsFile();
    }

    void markAsSent()
    {
        const auto dir = getFlagDirectory();
        dir.createDirectory();
        dir.getChildFile(kPingFlagFileName).replaceWithText(juce::Time::getCurrentTime().toISO8601(true));
    }

    // Fire-and-forget install telemetry for a free plugin. Runs off the audio
    // thread and must never surface anything to the user: no UI, no dialog, no
    // gating. A failure is simply retried on the next launch.
    void pingOnBackgroundThread()
    {
        juce::DynamicObject::Ptr payload = new juce::DynamicObject();
        payload->setProperty("pluginId", kPluginId);
        payload->setProperty("hwid", juce::SystemStats::getUniqueDeviceID());
        payload->setProperty("machineName", juce::SystemStats::getComputerName());
        payload->setProperty("pluginVersion", JucePlugin_VersionString);
        payload->setProperty("platform", juce::SystemStats::getOperatingSystemName());

        auto url = juce::URL(kEndpointBase + kPingPath).withPOSTData(juce::JSON::toString(payload.get()));

        int statusCode = 0;

        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withHttpRequestCmd("POST")
            .withConnectionTimeoutMs(4000)
            .withNumRedirectsToFollow(1)
            .withExtraHeaders("Content-Type: application/json")
            .withStatusCode(&statusCode);

        if (auto stream = url.createInputStream(opts))
            stream->readEntireStreamAsString();

        // Only latch the flag on an accepted ping, so a machine that was offline
        // on its first launch still gets counted on the next one.
        if (statusCode >= 200 && statusCode < 300)
            markAsSent();
    }
}

void opalineSendInstallPingOnce()
{
    if (hasAlreadySent())
        return;

    std::thread(pingOnBackgroundThread).detach();
}