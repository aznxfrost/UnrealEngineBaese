// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioCapture : ModuleRules
	{
        public AudioCapture(ReadOnlyTargetRules Target) : base(Target)
        {
            OptimizeCode = CodeOptimization.Never;

            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"CoreUObject",
					"Engine",
                    "AudioMixer",
                }
            );

            PrivateIncludePaths.AddRange(
                new string[] {            
                    "AudioCapture/Private"
                }
            );

            PublicIncludePaths.AddRange(
                new string[] {
                    "AudioCapture/Public"
                }
            );

            if (Target.Platform == UnrealTargetPlatform.Win32 ||
                Target.Platform == UnrealTargetPlatform.Win64)
            {
                Definitions.Add("WITH_RTAUDIO=1");
                Definitions.Add("WITH_AUDIOCAPTURE=1");

                // Allow us to use direct sound
                AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectSound");
            }
            else
            {
                // Not supported on this platform
                Definitions.Add("WITH_AUDIOCAPTURE=0");
            }
        }
    }
}