#pragma once

#include <functional>
#include "nlohmann/json.hpp"

namespace RC::Unreal {
    class FArrayProperty;
    class FProperty;
}

namespace Palworld::ArrayHelper {
    using CopyJsonValueToContainerCallback = std::function<void(void*, RC::Unreal::FProperty*, const nlohmann::json&)>;

    void ApplyPatchOperationsToArray(
        void* data,
        RC::Unreal::FArrayProperty* property,
        const nlohmann::json& patchData,
        const CopyJsonValueToContainerCallback& copyJsonValueToContainerCallback);
}
