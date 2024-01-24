#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"
#include "AssetValidationCommandlet.generated.h"

UCLASS()
class UAssetValidationCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()
	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

	bool FindFilesRecursive(const FString& Directory, TArray<FString>& Filenames);
	bool ValidateFilenames(const TArray<FString>& Filenames);
	bool ValidateFilenamesInternal(const FString Chaser, void* Data, const UStruct* Type, int32 ReferenceNumber);
	bool Validate(const FString Chaser, FString PackageName, UObject** Result = nullptr);
	bool Decomposite(const FString Chaser, void* Data, FProperty* Property, int32 ReferenceNumber);
	bool IsNeverValidate(const UObject* Target);
	bool IsRestricted(FString Chaser, int32& ReferenceNumber);
	bool IsRecursionStructure(const FString Chaser);
	bool ShouldSkipValidate(const FString& Filename);

private:

	int32 LimitNumber = 0;
	TArray<FString> LimitValidateTypes;
	TSet<FString> SkipFilenames;
};
