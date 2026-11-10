#include <minuvex/Telemetry.h>
#include <cstdio>
int main() {
    const auto hardware=lv::SystemInfo::QueryHardware();lv::SystemMonitor monitor;const auto sample=monitor.Sample(1);
    lv::QualityCapabilities caps;caps.GraphicsAvailable=true;caps.ShadowMapping=true;lv::Scene scene;
    const auto estimate=lv::EstimateQualityResources(lv::QualityProfile{},caps,scene,640,360);
    const auto recommendation=lv::RecommendQuality(caps,sample,scene,640,360);
    if (hardware.OperatingSystem.empty() || !estimate || estimate.Value().Cost.TotalBytes != 640ull*360*8+1024ull*1024*8 || !recommendation.Preset) {return 1;}
    std::printf("Hardware/telemetry CPU consumer: %s; logical render bytes %llu\n",hardware.OperatingSystem.c_str(),static_cast<unsigned long long>(estimate.Value().Cost.TotalBytes));return 0;
}
