#include <minvex/Asset.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <type_traits>

namespace lv {
namespace {
void Require(bool ok,const char* message) {if (!ok) {throw std::invalid_argument(message);}}
std::size_t EventBytes(std::span<const AnimationEvent> events) {
    std::size_t bytes=0;for (const auto& event:events) {bytes+=event.Name.size()+event.Payload.size();}return bytes;
}
bool Utf8(std::string_view text) {
    for (std::size_t i=0;i<text.size();) {const auto a=static_cast<unsigned char>(text[i++]);if (a<0x80) {if (!a) {return false;}continue;}unsigned value=0,minimum=0,count=0;
        if (a>=0xc2 && a<=0xdf) {value=a&31;minimum=0x80;count=1;}else if (a>=0xe0 && a<=0xef) {value=a&15;minimum=0x800;count=2;}else if (a>=0xf0 && a<=0xf4) {value=a&7;minimum=0x10000;count=3;}else {return false;}
        if (count>text.size()-i) {return false;}while (count--) {const auto b=static_cast<unsigned char>(text[i++]);if ((b&0xc0)!=0x80) {return false;}value=(value<<6)|(b&63);}if (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) {return false;}
    }return true;
}
}
void ValidateAnimationEvents(double duration,std::span<const AnimationEvent> events) {
    Require(std::isfinite(duration) && duration>=0 && events.size()<=MaxAnimationEvents,"Invalid event duration/count (at most 1024 per clip).");double previous=0;
    for (const auto& event:events) {Require(std::isfinite(event.TimeSeconds) && event.TimeSeconds>=previous && event.TimeSeconds<=duration,"Events must be sorted, finite and within clip duration.");previous=event.TimeSeconds;Require(!event.Name.empty() && event.Name.size()<=256 && event.Payload.size()<=4096 && Utf8(event.Name) && Utf8(event.Payload),"Event text must be UTF-8 without NUL; name 1..256 bytes, payload at most 4096 bytes.");}
    Require(EventBytes(events)<=MaxAnimationEventBytes,"Event track exceeds the 1 MiB metadata budget.");
}
std::span<const AnimationEvent> AnimationPlayer::AnimationEvents(std::size_t clip) const {
    Require(model_ && clip<model_->Animations.size(),"Event clip does not belong to this model.");for (const auto& track:eventOverrides_) {if (track.Clip==clip) {return track.Events;}}return model_->Animations[clip].Events;
}
void AnimationPlayer::SetAnimationEvents(std::size_t clip,std::span<const AnimationEvent> events) {
    Require(model_ && clip<model_->Animations.size(),"Event clip does not belong to this model.");ValidateAnimationEvents(model_->Animations[clip].Duration,events);auto next=eventOverrides_;
    auto found=std::find_if(next.begin(),next.end(),[&](const auto& track){return track.Clip==clip;});if (found==next.end()) {Require(next.size()<MaxAnimationEventTracks,"Too many event overlay tracks.");next.push_back({clip,{events.begin(),events.end()}});}else {found->Events.assign(events.begin(),events.end());}
    std::size_t bytes=0;for (const auto& track:next) {bytes+=EventBytes(track.Events);}Require(bytes<=MaxAnimationEventBytes,"Instance event overlays exceed 1 MiB UTF-8 text.");eventOverrides_=std::move(next);
}
void AnimationPlayer::ResetAnimationEvents(std::size_t clip) {(void)AnimationEvents(clip);std::erase_if(eventOverrides_,[&](const auto& track){return track.Clip==clip;});}
std::optional<std::size_t> AnimationPlayer::PrimaryEventClip() const {
    if (tree_) {const auto weights=BlendWeights();std::vector<std::pair<std::size_t,double>> clips;for (std::size_t i=0;i<weights.size();++i) {const auto clip=tree_->Tree->Description().Samples[i].Clip;const auto found=std::find_if(clips.begin(),clips.end(),[&](const auto& item){return item.first==clip;});if (found==clips.end()) {clips.push_back({clip,weights[i]});}else {found->second+=weights[i];}}return std::max_element(clips.begin(),clips.end(),[](const auto& a,const auto& b){return a.second<b.second;})->first;}return clip_;
}
void AnimationPlayer::CollectEvents(std::size_t clip,double oldTime,double newTime,bool loop,std::vector<AnimationEventOccurrence>* output,bool normalized) const {
    const auto track=AnimationEvents(clip);const auto duration=model_->Animations[clip].Duration;if (track.empty() || duration==0 || newTime==oldTime) {return;}
    Require(std::isfinite(newTime),"Event traversal exceeds finite time range.");const bool reverse=newTime<oldTime;const auto period=normalized ? 1.0:duration,end=loop ? newTime:std::clamp(newTime,0.0,period);if (end==oldTime) {return;}
    const auto available=MaxAnimationEventQueue-eventQueue_.size();if (loop && std::abs(newTime-oldTime)/period>static_cast<double>(available)+2) {throw AnimationEventOverflow("Animation event queue overflow; poll before advancing or reduce delta time.");}
    // Compare (cycle, local time), never cycle*duration+marker: large durations must not round a next-cycle marker onto the boundary.
    using Position=std::pair<long long,double>;
    const auto position=[&](double value)->Position {if (!loop) {return {0,normalized ? value*duration:value};}if (normalized) {const auto cycle=static_cast<long long>(std::floor(value));return {cycle,(value-static_cast<double>(cycle))*duration};}auto local=std::fmod(value,duration);auto cycle=std::llround((value-local)/duration);if (local<0) {--cycle;local+=duration;}return {cycle,local};};
    const auto from=position(oldTime),to=position(end);struct Candidate {Position Time;std::size_t Index;};std::vector<Candidate> candidates;std::size_t count=0;
    const auto collect=[&](std::size_t index,Position time) {if (reverse ? (time>=to && time<from):(time>from && time<=to)) {if (++count>available) {throw AnimationEventOverflow("Animation event queue overflow; player clocks and queued events are unchanged.");}if (output) {candidates.push_back({time,index});}}};
    const auto beginCycle=loop ? std::min(from.first,to.first)-1:0,endCycle=loop ? std::max(from.first,to.first)+1:0;
    for (auto cycle=beginCycle;cycle<=endCycle;++cycle) {for (std::size_t i=0;i<track.size();++i) {const auto time=track[i].TimeSeconds;collect(i,loop && time==duration ? Position{cycle+1,0}:Position{cycle,time});}}
    if (!output) {return;}
    std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b) {if (a.Time!=b.Time) {return reverse ? a.Time>b.Time:a.Time<b.Time;}const auto ta=track[a.Index].TimeSeconds,tb=track[b.Index].TimeSeconds;if (ta!=tb) {return reverse ? ta<tb:ta>tb;}return a.Index<b.Index;});
    output->reserve(output->size()+candidates.size());for (const auto& c:candidates) {output->push_back({model_->Id,clip,c.Index,model_->Animations[clip].Name,track[c.Index],reverse});}
}
void AnimationPlayer::CollectUpdateEvents(double delta,std::vector<AnimationEventOccurrence>* output) const {
    if (!model_ || state_!=PlaybackState::Playing || speed_==0 || delta==0) {return;}const auto clip=PrimaryEventClip();if (!clip || AnimationEvents(*clip).empty()) {return;}
    if (tree_) {const auto period=BlendTreeDuration();if (period==0) {return;}const auto phaseShift=delta*speed_/period;Require(std::isfinite(phaseShift) && std::isfinite(tree_->Phase+phaseShift),"Tree event phase exceeds numeric range.");CollectEvents(*clip,tree_->Phase,tree_->Phase+phaseShift,loop_,output,true);}
    else {const auto shift=delta*speed_;Require(std::isfinite(shift),"Event time advance exceeds numeric range.");CollectEvents(*clip,time_,time_+shift,loop_,output);}
}
void AnimationPlayer::QueueEvents(std::vector<AnimationEventOccurrence> events) {
    static_assert(std::is_nothrow_move_constructible_v<AnimationEventOccurrence>);if (events.empty()) {return;}if (events.size()>MaxAnimationEventQueue-eventQueue_.size()) {throw AnimationEventOverflow("Animation event queue overflow.");}eventQueue_.reserve(eventQueue_.size()+events.size());for (auto& event:events) {eventQueue_.push_back(std::move(event));}
}
std::vector<AnimationEventOccurrence> AnimationPlayer::PollAnimationEvents() {std::vector<AnimationEventOccurrence> result;result.swap(eventQueue_);return result;}
std::size_t AnimationPlayer::DispatchAnimationEvents(std::function<void(const AnimationEventOccurrence&)> callback) {
    Require(bool(callback),"Event dispatch requires a callback.");const auto events=PollAnimationEvents();std::exception_ptr failure;
    for (const auto& event:events) {try {callback(event);}catch (...) {if (!failure) {failure=std::current_exception();}}}if (failure) {throw AnimationEventCallbackError(failure);}return events.size();
}
}
