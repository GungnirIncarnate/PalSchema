#include "SDK/Classes/KismetInternationalizationLibrary.h"
#include "Unreal/UFunction.hpp"

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
	FString UKismetInternationalizationLibrary::GetCurrentLanguage()
	{
		static auto Function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, TEXT("/Script/Engine.KismetInternationalizationLibrary:GetCurrentLanguage"));

		struct {
			FString ReturnValue;
		}params;

		GetDefaultObj()->ProcessEvent(Function, &params);

		return params.ReturnValue;
	}

	UKismetInternationalizationLibrary* UKismetInternationalizationLibrary::GetDefaultObj()
	{
		static auto Self = UObjectGlobals::StaticFindObject<UKismetInternationalizationLibrary*>(nullptr, nullptr, TEXT("/Script/Engine.Default__KismetInternationalizationLibrary"));
		return Self;
	}
}