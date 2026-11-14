#include "CadenceSamples.h"

#include <cstdio>
#include <limits>

namespace {
int failures=0;
void Check(bool valid,const char* message) {if (!valid) {++failures;std::fprintf(stderr,"FAIL: %s\n",message);}}
template<class Function> void Rejects(Function function,const char* message) {
    try {function();Check(false,message);}catch (const std::invalid_argument&) {}
}
}
int main() {
    benchmark::CadenceSamples samples(2,3);
    samples.Record(0,0);samples.Record(1,10);samples.Record(1,15);samples.Record(2,20);
    Check(!samples.Complete() && samples.Intervals().empty(),"warmup and retried callbacks are not measured frames");
    samples.Record(3,40);samples.Record(3,45);samples.Record(4,60);
    Check(!samples.Complete(),"early window close cannot produce a complete benchmark");
    samples.Record(5,90);
    Check(samples.Complete() && samples.Intervals() == std::vector<double>{20,20,30},"exactly three complete frame intervals retain time spent retrying");
    Check(samples.Callbacks() == 8 && samples.DuplicateCallbacks() == 2,"duplicate callbacks remain visible in diagnostics");
    samples.Record(5,95);
    Check(samples.Intervals().size() == 3,"final frame callback retry cannot overfill the report");
    Rejects([&]{samples.Record(4,100);},"frame counter rollback is rejected");
    Rejects([]{benchmark::CadenceSamples s(0,1);s.Record(1,0);},"a missing initial frame is rejected");
    Rejects([]{benchmark::CadenceSamples s(0,1);s.Record(0,0);s.Record(2,10);},"skipped frame numbers cannot fabricate valid cadence");
    Rejects([]{benchmark::CadenceSamples s(0,1);s.Record(0,0);s.Record(1,0);},"non-increasing cadence is rejected");
    Rejects([]{benchmark::CadenceSamples s(0,1);s.Record(0,std::numeric_limits<double>::quiet_NaN());},"invalid clocks are rejected");
    Rejects([]{benchmark::CadenceSamples s(0,0);},"an empty measurement is rejected");
    std::printf("Benchmark cadence regression: %d failures\n",failures);
    return failures ? 1:0;
}
