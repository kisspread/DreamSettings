using UnrealBuildTool;

public class DreamSettings : ModuleRules
{
	public DreamSettings(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "AudioMixer", "EnhancedInput", "InputCore", "GameplayTags" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "AudioMixerCore", "Json", "Slate", "SlateCore", "ApplicationCore", "Projects" });
	}
}
