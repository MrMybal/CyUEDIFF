#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CyUEDiffDefaultLibrary.generated.h"
UCLASS()
class CYUEDIFFDEFAULTEXPORTER_API UCyUEDiffDefaultLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
  public:
    // Reads the selected loaded object and saved package-import inventory. Never saves assets.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Defaults")
    static FString Capture(UObject *Object, bool FilterEditorOnly = false);
    // Trusted preparation only: construct a temporary CDO seed from the explicit parent template.
    // The selected Blueprint class and its real default object must already be loaded.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Defaults")
    static FString CaptureBlueprintFactory(UClass* Class, bool FilterEditorOnly = false);
    // Own commandlet sessions can prevent shutdown/user-settings writes while collecting a pinned context.
    // Returns false in an interactive editor; the change lasts only for the commandlet process.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Defaults")
    static bool BeginReadOnlyCommandletCapture();
    // The selected host's UTF-16 uppercase mapping, not a guessed locale conversion.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Defaults")
    static FString CaptureNameCaseMap();
};
