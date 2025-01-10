#pragma once

#include "Unreal/UObject.hpp"
#include "SDK/Classes/TSoftObjectPtr.h"
#include "SDK/Classes/TSoftClassPtr.h"
#include "SDK/Classes/Texture2D.h"
#include "StaticMesh.h"

namespace Palworld {
    class UPalStaticItemDataBase : public RC::Unreal::UObject {
    public:
        static RC::Unreal::UClass* StaticClass();
    };
}