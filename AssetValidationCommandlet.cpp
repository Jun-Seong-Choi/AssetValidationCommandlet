#include "AssetValidationCommandlet.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetValidation, Log, All);

#define BASE_LOG_OFF   TEXT("Log LogUObjectBase off")
#define ARRAY_LOG_OFF   TEXT("Log LogUObjectArray off")
#define GLOBAL_LOG_OFF   TEXT("Log LogUObjectGlobals off")

#define TIME_LIMIT 1.0

const TCHAR* Delims[3] = { TEXT(">"),TEXT("["),TEXT("]") };

UAssetValidationCommandlet::UAssetValidationCommandlet(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

int32 UAssetValidationCommandlet::Main(const FString& Params)
{
    TArray<FString> Tokens;
    TArray<FString> Switches;
    ParseCommandLine(*Params, Tokens, Switches);

    FString Directory;
    FParse::Value(*Params, TEXT("Directory="), Directory);

    Directory = FPaths::Combine(FPaths::ProjectContentDir(), Directory);
    if (!FPaths::DirectoryExists(Directory))
    {
        return false;
    }

    TArray<FString> Filenames;
    if (!FindFilesRecursive(Directory, Filenames))
    {
        return false;
    }

    LimitValidateTypes = Switches.FilterByPredicate([](const FString& Str) {
        return !Str.IsEmpty() && Str.Contains("LimitType=");
        });
    if (LimitValidateTypes.Num())
    {
        for (FString& Type : LimitValidateTypes)
        {
            Type = Type.Replace(TEXT("LimitType="), TEXT(""));
        }
    }

    FParse::Value(*Params, TEXT("LimitNumber="), LimitNumber);

    GEngine->Exec(nullptr, BASE_LOG_OFF);
    GEngine->Exec(nullptr, ARRAY_LOG_OFF);
    GEngine->Exec(nullptr, GLOBAL_LOG_OFF);

    return ValidateFilenames(Filenames);
}

bool UAssetValidationCommandlet::FindFilesRecursive(const FString& Directory, TArray<FString>& Filenames)
{
    IFileManager::Get().FindFilesRecursive(Filenames, *Directory, TEXT("*"), true, false);
    return Filenames.Num() > 0;
}

bool UAssetValidationCommandlet::ValidateFilenames(const TArray<FString>& Filenames)
{
    bool bSuccessful = true;
    for (const FString& Filename : Filenames)
    {
        FString NewChaser;
        UObject* Target = nullptr;
        bSuccessful &= Validate(NewChaser, Filename, &Target);
        if (Target)
        {
            bSuccessful &= ValidateFilenamesInternal(NewChaser, (void*)Target, Target->GetClass(), 0);
        }
    }

    return !bSuccessful;
}

bool UAssetValidationCommandlet::ValidateFilenamesInternal(const FString Chaser, void* Data, const UStruct* Type, int32 ReferenceNumber)
{
    bool bSuccessful = true;
    if (Data)
    {
        for (TFieldIterator<FProperty> It(Type); It; ++It)
        {
            FString NextChaser = Chaser;
            NextChaser += FString::Format(TEXT("{0}{1}{2}{3}{4}{5}{6}"), { Delims[0], Delims[1], Type->GetName(), Delims[2], Delims[1], (*It)->GetName(), Delims[2] });
            FArrayProperty* Property = CastField<FArrayProperty>(*It);
            if (Property)
            {
                FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Data));
                for (int32 Index = 0; Index < Helper.Num(); Index++)
                {
                    void* NewData = Helper.GetRawPtr(Index);
                    bSuccessful &= Decomposite(NextChaser, NewData, Property->Inner, ReferenceNumber);
                }
            }
            else
            {
                bSuccessful &= Decomposite(NextChaser, Data, *It, ReferenceNumber);
            }
        }
    }

    return bSuccessful;
}

bool UAssetValidationCommandlet::Validate(const FString Chaser, FString PackageName, UObject** Result)
{
    bool bSuccessful = false;
    FString Filename;
    if (PackageName.IsEmpty() == false && FPackageName::DoesPackageExist(PackageName, &Filename))
    {
        const FString Extension = FPaths::GetExtension(Filename);
        if (FPaths::MakePathRelativeTo(Filename, *FPaths::ProjectContentDir()))
        {
            Filename = FString(TEXT("/Game/")) / Filename.LeftChop(Extension.Len() + 1);
        }

        if (!ShouldSkipValidate(Filename))
        {
            UE_LOG(LogAssetValidation, Display, TEXT("%s "), *Chaser);
            if (Filename.IsEmpty() == false)
            {
                const double Start = FPlatformTime::Seconds();
                UObject* Target = StaticLoadObject(UObject::StaticClass(), nullptr, *Filename);
                const double End = FPlatformTime::Seconds();
                if (Target)
                {
                    UE_LOG(LogAssetValidation, Display, TEXT("Filename = %s, Time = %lf Complete\n"), *Filename, End - Start);
                    bSuccessful = true;
                    if (Result)
                    {
                        *Result = Target;
                    }
                }
                else
                {
                    UE_LOG(LogAssetValidation, Error, TEXT("Filename=%s Fail\n"), *Filename);
                }
            }
        }
        else
        {
            bSuccessful = true;
        }
    }

    return bSuccessful;
}

bool UAssetValidationCommandlet::Decomposite(const FString Chaser, void* Data, FProperty* Property, int32 ReferenceNumber)
{
    bool bSuccessful = true;
    UObject* Target = nullptr;
    UStruct* Type = nullptr;

    FStructProperty* StructProperty = CastField<FStructProperty>(Property);
    if (StructProperty && StructProperty->Struct)
    {
        if (StructProperty->Struct->GetName().Equals(TEXT("SoftObjectPath")))
        {
            FSoftObjectPath Value;
            StructProperty->GetValue_InContainer(Data, &Value);
            bSuccessful &= Validate(Chaser, Value.GetAssetPathString(), &Target);
            if (Target)
            {
                ReferenceNumber++;
                Type = Target->GetClass();
            }
        }
        else
        {
            void* Value = nullptr;
            Value = StructProperty->ContainerPtrToValuePtr<void*>(Data);
            if (Value)
            {
                Target = (UObject*)Value;
                Type = StructProperty->Struct;
            }
        }
    }
    FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property);
    if (SoftObjectProperty)
    {
        FSoftObjectPtr Value;
        SoftObjectProperty->GetValue_InContainer(Data, &Value);
        bSuccessful &= Validate(Chaser, Value.ToString(), &Target);
        if (Target)
        {
            ReferenceNumber++;
            Type = Target->GetClass();
        }
    }
    FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
    if (ObjectProperty)
    {
        UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Data);
        if (Value)
        {
            Target = Value;
            Type = Value->GetClass();
        }
    }

    if (Target && !IsNeverValidate(Target) && !IsRecursionStructure(Chaser) && !IsRestricted(Chaser, ReferenceNumber))
    {
        bSuccessful &= ValidateFilenamesInternal(Chaser, (void*)Target, Type ? Type : Target->GetClass(), ReferenceNumber);
    }

    return bSuccessful;
}

bool UAssetValidationCommandlet::IsNeverValidate(const UObject* Target)
{
    if (Target && Target->GetClass() && Target->IsValidLowLevel())
    {
        for (const FString& Type : LimitValidateTypes)
        {
            if (Type.Equals(Target->GetClass()->GetName()))
            {
                return true;
            }
        }
    }

    return false;
}

bool UAssetValidationCommandlet::IsRestricted(FString Chaser, int32& ReferenceNumber)
{
    TArray<FString> Names;
    Chaser.ParseIntoArray(Names, Delims[0]);
    if (Names.Num() > 0)
    {
        FString Delim = FString::Format(TEXT("{1}{0}"), { Delims[1], Delims[2] });
        for (int32 Index = 0; Index < Names.Num(); Index++)
        {
            TArray<FString> Name;
            Names[Index].ParseIntoArray(Name, *Delim);
            if (Name.Num())
            {
                Name[0] = Name[0].Replace(Delims[1], TEXT(""));
                for (const FString& LimitType : LimitValidateTypes)
                {
                    if (Name[0].Equals(LimitType) && ReferenceNumber == LimitNumber)
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool UAssetValidationCommandlet::IsRecursionStructure(const FString Chaser)
{
    TArray<FString> Names;
    Chaser.ParseIntoArray(Names, Delims[0]);
    if (Names.Num() > 0)
    {
        FString Delim = FString::Format(TEXT("{0}{1}"), { Delims[2], Delims[1] });
        TArray<FString> LastNames;
        Names[Names.Num() - 1].ParseIntoArray(LastNames, *Delim);
        if (LastNames.Num() == 2)
        {
            LastNames[0] = LastNames[0].Replace(Delims[1], TEXT(""));
            LastNames[1] = LastNames[1].Replace(Delims[2], TEXT(""));
            for (int32 Index = 0; Index < Names.Num() - 1; Index++)
            {
                TArray<FString> Name;
                Names[Index].ParseIntoArray(Name, *Delim);
                if (Name.Num() == 2)
                {
                    Name[0] = Name[0].Replace(Delims[1], TEXT(""));
                    Name[1] = Name[1].Replace(Delims[2], TEXT(""));
                    if (Name[0] == LastNames[0] && Name[1] == LastNames[1])
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool UAssetValidationCommandlet::ShouldSkipValidate(const FString& Filename)
{
    if (!SkipFilenames.Find(Filename))
    {
        SkipFilenames.Add(Filename);
        return false;
    }

    return true;
}