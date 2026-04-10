#pragma once

#include "Unreal/AActor.hpp"

namespace RC::Unreal {
    class UObject;
}

namespace Palworld {
    class AMonoNPCSpawner : public RC::Unreal::AActor {
    public:
        int& GetLevel();

        RC::Unreal::FName& GetHumanName();

        RC::Unreal::FName& GetCharaName();

        RC::Unreal::FName& GetOtomoName();

        RC::Unreal::UObject* GetSpawnedHandle();

        RC::Unreal::AActor* TryGetSpawnedCharacter();
    };
}