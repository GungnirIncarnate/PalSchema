#include "SDK/Classes/AMonoNPCSpawner.h"
#include "Unreal/UnrealFlags.hpp"

using namespace RC::Unreal;

namespace Palworld {
    int& AMonoNPCSpawner::GetLevel()
    {
        auto Value = this->GetValuePtrByPropertyNameInChain<int>(TEXT("Level"));
        return *Value;
    }

    RC::Unreal::FName& AMonoNPCSpawner::GetHumanName()
    {
        auto Value = this->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(TEXT("HumanName"));
        return *Value;
    }

    RC::Unreal::FName& AMonoNPCSpawner::GetCharaName()
    {
        auto Value = this->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(TEXT("CharaName"));
        return *Value;
    }

    RC::Unreal::FName& AMonoNPCSpawner::GetOtomoName()
    {
        auto Value = this->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(TEXT("OtomoName"));
        return *Value;
    }

    RC::Unreal::UObject* AMonoNPCSpawner::GetSpawnedHandle()
    {
        if (!this)
        {
            return nullptr;
        }

        if (!RC::Unreal::UObject::IsReal(this))
        {
            return nullptr;
        }

        if (!this->GetClassPrivate())
        {
            return nullptr;
        }

        if (this->HasAnyFlags(RC::Unreal::RF_ClassDefaultObject))
        {
            return nullptr;
        }

        auto Value = this->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(TEXT("SpawnedHandle"));
        if (!Value)
        {
            return nullptr;
        }

        return *Value;
    }

    RC::Unreal::AActor* AMonoNPCSpawner::TryGetSpawnedCharacter()
    {
        if (!this)
        {
            return nullptr;
        }

        if (!RC::Unreal::UObject::IsReal(this))
        {
            return nullptr;
        }

        if (!this->GetClassPrivate())
        {
            return nullptr;
        }

        if (this->HasAnyFlags(RC::Unreal::RF_ClassDefaultObject))
        {
            return nullptr;
        }

        auto* spawnedHandle = GetSpawnedHandle();
        if (!spawnedHandle)
        {
            return nullptr;
        }

        if (!RC::Unreal::UObject::IsReal(spawnedHandle))
        {
            return nullptr;
        }

        auto* Function = spawnedHandle->GetFunctionByNameInChain(TEXT("TryGetIndividualActor"));
        if (!Function)
        {
            return nullptr;
        }

        struct {
            RC::Unreal::AActor* ReturnValue = nullptr;
        } params{};

        spawnedHandle->ProcessEvent(Function, &params);
        return params.ReturnValue;
    }

}