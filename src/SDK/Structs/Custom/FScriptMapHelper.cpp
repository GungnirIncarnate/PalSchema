#include "SDK/Structs/Custom/FScriptMapHelper.h"
#include "Unreal/FProperty.hpp"

using namespace RC;
using namespace RC::Unreal;

namespace UECustom {
    FScriptMapHelper::FScriptMapHelper(RC::Unreal::FMapProperty* InProperty, void* InMap)
    {
        KeyProperty = InProperty->GetKeyProp();
        ValueProperty = InProperty->GetValueProp();

        MapLayout = FScriptMap::GetScriptLayout(
            KeyProperty->GetSize(),
            KeyProperty->GetMinAlignment(),
            ValueProperty->GetSize(),
            ValueProperty->GetMinAlignment()
        );

        ScriptMap = static_cast<FScriptMap*>(InMap);
    }

    FScriptMapHelper::FScriptMapHelper(FScriptMap* InScriptMap, FScriptMapLayout InMapLayout, FProperty* InKeyProperty, FProperty* InValueProperty)
    {
        ScriptMap = InScriptMap;
        MapLayout = InMapLayout;
        KeyProperty = InKeyProperty;
        ValueProperty = InValueProperty;
    }

    void FScriptMapHelper::Add(void* PairPtrToAdd)
    {
        // Try to check if the key exists first. If it exists, update and return, otherwise continue adding a new key.
        if (Update(PairPtrToAdd)) return;

        auto Index = ScriptMap->AddUninitialized(MapLayout);
        uint8* PairPtr = static_cast<uint8*>(ScriptMap->GetData(Index, MapLayout));

        void* KeyPtr = PairPtr;
        void* ValuePtr = PairPtr + MapLayout.ValueOffset;

        KeyProperty->InitializeValue(KeyPtr);
        ValueProperty->InitializeValue(ValuePtr);

        void* KeyPtrToAdd = PairPtrToAdd;
        void* ValuePtrToAdd = static_cast<uint8*>(PairPtrToAdd) + MapLayout.ValueOffset;

        FMemory::Memcpy(KeyPtr, KeyPtrToAdd, KeyProperty->GetElementSize());
        FMemory::Memcpy(ValuePtr, ValuePtrToAdd, ValueProperty->GetElementSize());
    }

    void FScriptMapHelper::Add(UECustom::FManagedValue& PairPtr)
    {
        Add(PairPtr.GetData());
    }

    bool FScriptMapHelper::Update(void* PairPtrToUpdate)
    {
        auto Num = ScriptMap->Num();

        if (Num < 0)
        {
            // Better to throw here since we're going to crash anyway. Num should never be negative.
            throw std::runtime_error("Failed to update TMap entry due to invalid ScriptMap.");
        }

        void* KeyPtrToUpdate = PairPtrToUpdate;
        void* ValuePtrToUpdate = static_cast<uint8*>(PairPtrToUpdate) + MapLayout.ValueOffset;

        for (auto Index = 0; Index < Num; ++Index)
        {
            if (!ScriptMap->IsValidIndex(Index)) {
                continue;
            }

            uint8* PairPtr = (uint8*)ScriptMap->GetData(Index, MapLayout);
            void* KeyPtr = PairPtr;
            void* ValuePtr = PairPtr + MapLayout.ValueOffset;

            if (KeyProperty->Identical(KeyPtr, KeyPtrToUpdate))
            {
                FMemory::Memcpy(KeyPtr, KeyPtrToUpdate, KeyProperty->GetElementSize());
                FMemory::Memcpy(ValuePtr, ValuePtrToUpdate, ValueProperty->GetElementSize());
                return true;
            }
        }

        return false;
    }

    bool FScriptMapHelper::SetValueByKey(void* KeyPtrToSet, void* ValuePtrToSet, bool AddIfMissing)
    {
        auto Num = ScriptMap->Num();

        if (Num < 0)
        {
            throw std::runtime_error("Failed to set TMap entry due to invalid ScriptMap.");
        }

        for (auto Index = 0; Index < Num; ++Index)
        {
            if (!ScriptMap->IsValidIndex(Index))
            {
                continue;
            }

            uint8* PairPtr = static_cast<uint8*>(ScriptMap->GetData(Index, MapLayout));
            void* ExistingKeyPtr = PairPtr;
            void* ExistingValuePtr = PairPtr + MapLayout.ValueOffset;

            if (!KeyProperty->Identical(ExistingKeyPtr, KeyPtrToSet))
            {
                continue;
            }

            FMemory::Memcpy(ExistingValuePtr, ValuePtrToSet, ValueProperty->GetElementSize());
            return true;
        }

        if (!AddIfMissing)
        {
            return false;
        }

        auto Index = ScriptMap->AddUninitialized(MapLayout);
        uint8* PairPtr = static_cast<uint8*>(ScriptMap->GetData(Index, MapLayout));

        void* NewKeyPtr = PairPtr;
        void* NewValuePtr = PairPtr + MapLayout.ValueOffset;

        KeyProperty->InitializeValue(NewKeyPtr);
        ValueProperty->InitializeValue(NewValuePtr);

        FMemory::Memcpy(NewKeyPtr, KeyPtrToSet, KeyProperty->GetElementSize());
        FMemory::Memcpy(NewValuePtr, ValuePtrToSet, ValueProperty->GetElementSize());
        return true;
    }

    bool FScriptMapHelper::Remove(void* KeyToRemove)
    {
        auto Num = ScriptMap->Num();

        if (Num < 0)
        {
            // Better to throw here since we're going to crash anyway. Num should never be negative.
            throw std::runtime_error("Failed to remove TMap entry due to invalid ScriptMap.");
        }

        for (auto Index = 0; Index < Num; ++Index)
        {
            if (!ScriptMap->IsValidIndex(Index)) {
                continue;
            }

            uint8* PairPtr = (uint8*)ScriptMap->GetData(Index, MapLayout);
            void* KeyPtr = PairPtr;

            if (KeyProperty->Identical(KeyPtr, KeyToRemove))
            {
                ScriptMap->RemoveAt(Index, MapLayout);
                return true;
            }
        }

        return false;
    }

    void FScriptMapHelper::InitializePair(UECustom::FManagedValue& PairPtr)
    {
        uint8* Pair = static_cast<uint8*>(FMemory::Malloc(KeyProperty->GetElementSize() + ValueProperty->GetElementSize()));

        void* KeyPtr = Pair;
        KeyProperty->InitializeValue(KeyPtr);

        void* ValuePtr = Pair + MapLayout.ValueOffset;
        ValueProperty->InitializeValue(ValuePtr);

        PairPtr.Copy(Pair);
    }

    void FScriptMapHelper::ForEachPair(const std::function<void(void*, void*)> Callback)
    {
        for (int32 Index = 0; Index < ScriptMap->GetMaxIndex(); ++Index)
        {
            if (!ScriptMap->IsValidIndex(Index)) continue;

            uint8* PairPtr = (uint8*)ScriptMap->GetData(Index, MapLayout);
            void* ValuePtr = PairPtr + MapLayout.ValueOffset;

            Callback(PairPtr, ValuePtr);
        }
    }
}