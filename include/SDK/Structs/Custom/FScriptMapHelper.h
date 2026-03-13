#pragma once

#include "Unreal/Property/FMapProperty.hpp"
#include "SDK/Structs/Custom/FManagedValue.h"

namespace UECustom {
    // NOTE: This is not related to the real FScriptMapHelper at all, this is a custom implementation.
    struct FScriptMapHelper {
    public:
        FScriptMapHelper(RC::Unreal::FMapProperty* InProperty, void* InMap);

        FScriptMapHelper(RC::Unreal::FScriptMap* InScriptMap, RC::Unreal::FScriptMapLayout InMapLayout, RC::Unreal::FProperty* InKeyProperty, RC::Unreal::FProperty* InValueProperty);

        void Add(void* PairPtrToAdd);

        void Add(UECustom::FManagedValue& PairPtr);

        // Returns true if an entry was found and updated, otherwise returns false in cases where a key didn't exist inside the TMap.
        bool Update(void* PairPtrToUpdate);

        // Sets map value for an existing key and optionally adds a new entry when missing.
        // Returns true if a value was written, false only when key is missing and add is disabled.
        bool SetValueByKey(void* KeyPtr, void* ValuePtr, bool AddIfMissing = true);

        // Returns true if an entry was deleted, otherwise returns false in cases where a key didn't exist inside the TMap.
        bool Remove(void* KeyToRemove);

        void InitializePair(UECustom::FManagedValue& PairPtr);

        void ForEachPair(const std::function<void(void*, void*)> Callback);
    private:
        RC::Unreal::FScriptMap* ScriptMap{};
        RC::Unreal::FScriptMapLayout MapLayout{};
        RC::Unreal::FProperty* KeyProperty{};
        RC::Unreal::FProperty* ValueProperty{};
    };
}