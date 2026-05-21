// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ImSlate : ModuleRules
{
	public ImSlate(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[] {
			// ... add public include paths required here ...
		});

		PrivateIncludePaths.AddRange(new string[] {
			// ... add other private include paths required here ...
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			//
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			// ... add other public dependencies that you statically link with here ...
			"GMP",
			"GenericStorages",
			"Slate",
			"SlateCore",
			"InputCore",
			"UMG",
			"Json",
			"ApplicationCore",
			"AppFramework",
			"RenderCore",
			"EnhancedInput",
			"AssetRegistry",
            //"AnimatedTexture",
			// ... add private dependencies that you statically link with here ...
		});
		if (Target.Type == TargetRules.TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"LevelEditor",
				"PropertyEditor",
			});
		}
		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});
	}
}
