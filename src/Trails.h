#pragma once
#include <string_view>
namespace bf::trails {
void OnAnimationEvent(std::string_view tag); // event sink: queues only
void Tick();                               // game thread only
void Reset();                              // game thread only, save/load cleanup
}
