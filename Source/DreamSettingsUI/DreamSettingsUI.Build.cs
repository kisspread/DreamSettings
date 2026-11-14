using UnrealBuildTool;

public class DreamSettingsUI : ModuleRules
{
    public DreamSettingsUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "DreamSettings", "DreamGUI", "DreamGUIControls", "DreamGUIInput", "FieldNotification" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "DreamGUIRenderer" });
    }
}
