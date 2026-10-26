#pragma once
namespace RE { class PlayerCharacter; }
namespace bf::stabilize {
void Install();
void SetWeight(RE::PlayerCharacter* player, float weight);
void Reset();
}
