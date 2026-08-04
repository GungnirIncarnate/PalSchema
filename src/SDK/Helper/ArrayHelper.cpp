#include "SDK/Helper/ArrayHelper.h"
#include "Unreal/CoreUObject/UObject/UnrealType.hpp"
#include "SDK/Structs/Custom/FManagedValue.h"
#include "SDK/Structs/Custom/FScriptArrayHelper.h"

using namespace RC;
using namespace RC::Unreal;

namespace Palworld::ArrayHelper {
    namespace {
        int32 ParseIndex(const nlohmann::json& operation)
        {
            if (!operation.contains("Index") || !operation.at("Index").is_number_integer())
            {
                throw std::runtime_error("Array patch operation requires an integer 'Index' field.");
            }

            return operation.at("Index").get<int32>();
        }

        void ValidateHasValueField(const nlohmann::json& operation)
        {
            if (!operation.contains("Value"))
            {
                throw std::runtime_error("Array patch operation requires a 'Value' field.");
            }
        }
    }

    void ApplyPatchOperationsToArray(
        void* data,
        FArrayProperty* property,
        const nlohmann::json& patchData,
        const CopyJsonValueToContainerCallback& copyJsonValueToContainerCallback)
    {
        if (!patchData.is_object())
        {
            throw std::runtime_error("Array patch payload must be an object.");
        }

        if (!patchData.contains("Operations") || !patchData.at("Operations").is_array())
        {
            throw std::runtime_error("Array patch payload must contain an array field named 'Operations'.");
        }

        auto scriptArray = static_cast<FScriptArray*>(data);
        auto scriptArrayHelper = UECustom::FScriptArrayHelper(scriptArray, property);
        auto innerProperty = property->GetInner();

        for (const auto& operation : patchData.at("Operations"))
        {
            if (!operation.is_object())
            {
                throw std::runtime_error("Array patch operations must be objects.");
            }

            if (!operation.contains("Type") || !operation.at("Type").is_string())
            {
                throw std::runtime_error("Array patch operation requires a string 'Type' field.");
            }

            auto operationType = operation.at("Type").get<std::string>();
            if (operationType == "Append")
            {
                ValidateHasValueField(operation);

                UECustom::FManagedValue valuePtr;
                scriptArrayHelper.InitializeValue(valuePtr);
                copyJsonValueToContainerCallback(valuePtr.GetData(), innerProperty, operation.at("Value"));
                scriptArrayHelper.Add(valuePtr);
                continue;
            }

            if (operationType == "RemoveAt")
            {
                auto index = ParseIndex(operation);
                if (!scriptArrayHelper.RemoveAtIndex(index))
                {
                    throw std::runtime_error(std::format("Array patch operation 'RemoveAt' failed. Index {} was out of range.", index));
                }

                continue;
            }

            if (operationType == "EditAt")
            {
                ValidateHasValueField(operation);

                auto index = ParseIndex(operation);
                if (!scriptArray->IsValidIndex(index))
                {
                    throw std::runtime_error(std::format("Array patch operation 'EditAt' failed. Index {} was out of range.", index));
                }

                auto elementSize = innerProperty->GetElementSize();
                auto elementPtr = static_cast<uint8*>(scriptArray->GetData()) + index * elementSize;

                copyJsonValueToContainerCallback(elementPtr, innerProperty, operation.at("Value"));
                continue;
            }

            throw std::runtime_error(std::format(
                "Unsupported array patch operation type '{}'. Supported operation types are Append, EditAt and RemoveAt.", operationType));
        }
    }
}
