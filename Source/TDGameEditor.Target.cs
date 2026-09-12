// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class TDGameEditorTarget : TargetRules
{
	public TDGameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("TDGame");
		ExtraModuleNames.Add("TDWorldGen");
		ExtraModuleNames.Add("TDGameEditor");
	}
}
