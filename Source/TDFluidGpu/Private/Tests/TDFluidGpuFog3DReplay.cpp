#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/TDFluidGpuFog3DReplay.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TDFluidGpuFog3DReplayPrivate
{
	constexpr TCHAR ReplayFormatName[] = TEXT("fog3d-replay-1");
	constexpr TCHAR ReplayCurrentWindBlendName[] = TEXT("B0");
	constexpr TCHAR ReplayClampedTimeWindBlendName[] = TEXT("B4");
	constexpr int32 ReplayLayerCount = 8;
	constexpr double ReplayCellSizeCm = 15.625;
	constexpr double ReplayLayerHeightCm = 30.0;
	constexpr double ReplayHalfLayerHeightCm = 15.0;
	constexpr int32 ReplayMaximumRegionDepth = 16;

	const TCHAR* const ReplayDumpFieldNames[TDFog3DReplayDumpFieldCount] = {
		TEXT("u"), TEXT("v"), TEXT("w"), TEXT("dx"), TEXT("dy"), TEXT("dz"), TEXT("g"), TEXT("pressure"), TEXT("divergence"), TEXT("fluid") };

	int32 FindReplayDumpFieldIndex(const FString& FieldName)
	{
		for (int32 FieldIndex = 0; FieldIndex < TDFog3DReplayDumpFieldCount; ++FieldIndex)
		{
			if (FieldName == ReplayDumpFieldNames[FieldIndex])
			{
				return FieldIndex;
			}
		}
		return INDEX_NONE;
	}

	bool ReadNumber(const FJsonObject& Object, const TCHAR* Name, double& OutValue)
	{
		return Object.TryGetNumberField(Name, OutValue);
	}

	bool ReadInteger(const FJsonObject& Object, const TCHAR* Name, int32& OutValue)
	{
		double Value = 0.0;
		if (!Object.TryGetNumberField(Name, Value))
		{
			return false;
		}
		OutValue = FMath::RoundToInt(Value);
		return true;
	}

	bool ReadNumberArray(const FJsonObject& Object, const TCHAR* Name, int32 ExpectedCount, TArray<double>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.TryGetArrayField(Name, Values) || Values->Num() != ExpectedCount)
		{
			return false;
		}
		OutValues.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			double Number = 0.0;
			if (!Value.IsValid() || !Value->TryGetNumber(Number))
			{
				return false;
			}
			OutValues.Add(Number);
		}
		return true;
	}

	bool ReadVector(const FJsonObject& Object, const TCHAR* Name, FVector3d& OutVector)
	{
		TArray<double> Values;
		if (!ReadNumberArray(Object, Name, 3, Values))
		{
			return false;
		}
		OutVector = FVector3d(Values[0], Values[1], Values[2]);
		return true;
	}

	bool ReadCellPoint(const FJsonObject& Object, const TCHAR* Name, FIntPoint& OutPoint)
	{
		TArray<double> Values;
		if (!ReadNumberArray(Object, Name, 2, Values))
		{
			return false;
		}
		OutPoint = FIntPoint(FMath::RoundToInt(Values[0]), FMath::RoundToInt(Values[1]));
		return true;
	}

	bool ParseRegion(const TSharedPtr<FJsonObject>& Object, int32 Depth, TSharedPtr<FTDFog3DReplayRegion>& OutRegion, FString& OutError)
	{
		FString KindName;
		if (!Object.IsValid() || Depth > ReplayMaximumRegionDepth || !Object->TryGetStringField(TEXT("kind"), KindName))
		{
			OutError = TEXT("region without kind");
			return false;
		}

		TSharedRef<FTDFog3DReplayRegion> Region = MakeShared<FTDFog3DReplayRegion>();
		bool bIsValid = false;
		if (KindName == TEXT("all"))
		{
			Region->Kind = ETDFog3DReplayRegionKind::All;
			bIsValid = true;
		}
		else if (KindName == TEXT("aabb"))
		{
			Region->Kind = ETDFog3DReplayRegionKind::Aabb;
			bIsValid = ReadVector(*Object, TEXT("min_cm"), Region->MinimumCm) && ReadVector(*Object, TEXT("max_cm"), Region->MaximumCm);
		}
		else if (KindName == TEXT("halfspace") || KindName == TEXT("slab"))
		{
			const bool bIsSlab = KindName == TEXT("slab");
			Region->Kind = bIsSlab ? ETDFog3DReplayRegionKind::Slab : ETDFog3DReplayRegionKind::Halfspace;
			bIsValid = ReadVector(*Object, TEXT("point_cm"), Region->PointCm) && ReadVector(*Object, TEXT("normal"), Region->Normal)
				&& ReadNumber(*Object, TEXT("z_min_cm"), Region->HeightMinimumCm) && ReadNumber(*Object, TEXT("z_max_cm"), Region->HeightMaximumCm);
			if (bIsValid && bIsSlab)
			{
				bIsValid = ReadNumber(*Object, TEXT("d_min_cm"), Region->DistanceMinimumCm) && ReadNumber(*Object, TEXT("d_max_cm"), Region->DistanceMaximumCm);
			}
		}
		else if (KindName == TEXT("cell_box"))
		{
			Region->Kind = ETDFog3DReplayRegionKind::CellBox;
			bIsValid = ReadInteger(*Object, TEXT("i0"), Region->CellMinimum.X) && ReadInteger(*Object, TEXT("j0"), Region->CellMinimum.Y)
				&& ReadInteger(*Object, TEXT("k0"), Region->CellMinimum.Z) && ReadInteger(*Object, TEXT("i1"), Region->CellMaximum.X)
				&& ReadInteger(*Object, TEXT("j1"), Region->CellMaximum.Y) && ReadInteger(*Object, TEXT("k1"), Region->CellMaximum.Z);
		}
		else if (KindName == TEXT("not"))
		{
			Region->Kind = ETDFog3DReplayRegionKind::Not;
			const TSharedPtr<FJsonObject>* ChildObject = nullptr;
			TSharedPtr<FTDFog3DReplayRegion> Child;
			bIsValid = Object->TryGetObjectField(TEXT("region"), ChildObject) && ParseRegion(*ChildObject, Depth + 1, Child, OutError);
			if (bIsValid)
			{
				Region->Children.Add(Child);
			}
		}
		else if (KindName == TEXT("union"))
		{
			Region->Kind = ETDFog3DReplayRegionKind::Union;
			const TArray<TSharedPtr<FJsonValue>>* ChildValues = nullptr;
			bIsValid = Object->TryGetArrayField(TEXT("regions"), ChildValues);
			for (int32 ChildIndex = 0; bIsValid && ChildIndex < ChildValues->Num(); ++ChildIndex)
			{
				const TSharedPtr<FJsonObject>* ChildObject = nullptr;
				TSharedPtr<FTDFog3DReplayRegion> Child;
				bIsValid = (*ChildValues)[ChildIndex]->TryGetObject(ChildObject) && ParseRegion(*ChildObject, Depth + 1, Child, OutError);
				if (bIsValid)
				{
					Region->Children.Add(Child);
				}
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("unknown region kind %s"), *KindName);
			return false;
		}

		if (!bIsValid)
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("region %s has missing fields"), *KindName);
			}
			return false;
		}
		OutRegion = Region;
		return true;
	}

	bool ParseWind(const TSharedPtr<FJsonObject>& Object, FTDFog3DReplayWind& OutWind, FString& OutError)
	{
		FString KindName;
		bool bIsValid = false;
		if (!Object->TryGetStringField(TEXT("kind"), KindName))
		{
			OutError = TEXT("wind_override without kind");
			return false;
		}
		if (KindName == TEXT("uniform"))
		{
			OutWind.Kind = ETDFog3DReplayWindKind::Uniform;
			bIsValid = ReadVector(*Object, TEXT("v_cm_s"), OutWind.UniformCmPerSec);
		}
		else if (KindName == TEXT("vertical_osc"))
		{
			OutWind.Kind = ETDFog3DReplayWindKind::VerticalOscillation;
			bIsValid = ReadNumber(*Object, TEXT("amp_cm_s"), OutWind.AmplitudeCmPerSec) && ReadNumber(*Object, TEXT("period_s"), OutWind.PeriodSeconds)
				&& ReadNumber(*Object, TEXT("t_s"), OutWind.TimeSeconds);
		}
		else if (KindName == TEXT("vortex"))
		{
			OutWind.Kind = ETDFog3DReplayWindKind::Vortex;
			bIsValid = ReadNumber(*Object, TEXT("u_cm_s"), OutWind.VortexSpeedCmPerSec) && ReadNumber(*Object, TEXT("half_period_cm"), OutWind.VortexHalfPeriodCm)
				&& ReadNumber(*Object, TEXT("sign"), OutWind.VortexSign);
		}
		else
		{
			OutError = FString::Printf(TEXT("unknown wind_override kind %s"), *KindName);
			return false;
		}
		if (!bIsValid)
		{
			OutError = FString::Printf(TEXT("wind_override %s has missing fields"), *KindName);
		}
		return bIsValid;
	}

	bool ParseSphere(const FJsonObject& Object, FTDFog3DReplaySphere& OutSphere)
	{
		OutSphere.bHasOcclusionAnchor = Object.HasField(TEXT("occlusion_anchor_cm"));
		return ReadVector(Object, TEXT("a_cm"), OutSphere.StartCm) && ReadVector(Object, TEXT("b_cm"), OutSphere.EndCm) && ReadNumber(Object, TEXT("r_cm"), OutSphere.RadiusCm)
			&& ReadVector(Object, TEXT("v_cm_s"), OutSphere.VelocityCmPerSec) && ReadNumber(Object, TEXT("s"), OutSphere.WindStrength)
			&& ReadNumber(Object, TEXT("e_per_sec"), OutSphere.ErosionPerSec) && (!Object.HasField(TEXT("g")) || ReadNumber(Object, TEXT("g"), OutSphere.WindGain))
			&& (!OutSphere.bHasOcclusionAnchor || ReadVector(Object, TEXT("occlusion_anchor_cm"), OutSphere.OcclusionAnchorCm));
	}

	bool HasReplayStepWindGain(const FTDFog3DReplayStep& Step)
	{
		return Step.Spheres.ContainsByPredicate([](const FTDFog3DReplaySphere& Sphere) { return Sphere.WindGain != 1.0; });
	}

	bool ParseTuning(const FJsonObject& Object, FTDFluidGpuFog3DTuning& OutTuning, FTDFluidGpuFog3DInflation& OutInflation)
	{
		double WindRelax = 0.0;
		double HorizontalRelax = 0.0;
		double VerticalRelax = 0.0;
		double FillRelax = 0.0;
		double BandCells = 0.0;
		double BandRate = 0.0;
		double Sweeps = 0.0;
		double Omega = 0.0;
		double MoveHorizontal = 0.0;
		double MoveVertical = 0.0;
		double InflateHorizontal = 0.0;
		double InflateVertical = 0.0;
		const bool bIsValid = ReadNumber(Object, TEXT("wind_relax_per_sec"), WindRelax) && ReadNumber(Object, TEXT("disp_relax_h_per_sec"), HorizontalRelax)
			&& ReadNumber(Object, TEXT("disp_relax_v_per_sec"), VerticalRelax) && ReadNumber(Object, TEXT("fill_relax_per_sec"), FillRelax)
			&& ReadNumber(Object, TEXT("band_cells"), BandCells) && ReadNumber(Object, TEXT("band_rate_per_sec"), BandRate)
			&& ReadNumber(Object, TEXT("pressure_sweeps"), Sweeps) && ReadNumber(Object, TEXT("pressure_omega"), Omega)
			&& ReadNumber(Object, TEXT("move_cap_h_cm"), MoveHorizontal) && ReadNumber(Object, TEXT("move_cap_v_cm"), MoveVertical)
			&& ReadNumber(Object, TEXT("inflate_h_cm"), InflateHorizontal) && ReadNumber(Object, TEXT("inflate_v_extra_cm"), InflateVertical);
		if (!bIsValid)
		{
			return false;
		}
		OutTuning.WindRefillPerSec = static_cast<float>(WindRelax);
		OutTuning.DisplacementRefillHorizontalPerSec = static_cast<float>(HorizontalRelax);
		OutTuning.DisplacementRefillVerticalPerSec = static_cast<float>(VerticalRelax);
		OutTuning.FillRefillPerSec = static_cast<float>(FillRelax);
		OutTuning.EdgeBandCells = FMath::RoundToInt(BandCells);
		OutTuning.EdgeBandAbsorptionPerSec = static_cast<float>(BandRate);
		OutTuning.PressureSweepCount = FMath::RoundToInt(Sweeps);
		OutTuning.PressureOverRelaxation = static_cast<float>(Omega);
		OutTuning.MoveLimitHorizontalCm = static_cast<float>(MoveHorizontal);
		OutTuning.MoveLimitVerticalCm = static_cast<float>(MoveVertical);
		OutInflation.BaseHalfThicknessCm = static_cast<float>(InflateHorizontal);
		OutInflation.UpwardExtraCm = static_cast<float>(InflateVertical);
		return true;
	}

	bool ParseObstacleList(const TArray<TSharedPtr<FJsonValue>>& ObstacleValues, TArray<FTDFog3DReplayObstacle>& OutObstacles, FString& OutError)
	{
		if (ObstacleValues.Num() > TDFluidGpuFog3DMaximumObstacleCount)
		{
			OutError = FString::Printf(TEXT("obstacle list has %d entries, more than %d"), ObstacleValues.Num(), TDFluidGpuFog3DMaximumObstacleCount);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& ObstacleValue : ObstacleValues)
		{
			const TSharedPtr<FJsonObject>* ObstacleObject = nullptr;
			FTDFog3DReplayObstacle Obstacle;
			if (!ObstacleValue->TryGetObject(ObstacleObject) || !ReadVector(**ObstacleObject, TEXT("center_cm"), Obstacle.CenterCm)
				|| !ReadVector(**ObstacleObject, TEXT("half_cm"), Obstacle.HalfExtentCm) || !ReadNumber(**ObstacleObject, TEXT("yaw_deg"), Obstacle.YawDegrees))
			{
				OutError = TEXT("obstacle has missing fields");
				return false;
			}
			OutObstacles.Add(Obstacle);
		}
		return true;
	}

	bool ParseStepObstacles(const FJsonObject& Object, FTDFog3DReplayStep& OutStep, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* ObstacleValues = nullptr;
		if (!Object.TryGetArrayField(TEXT("obstacles"), ObstacleValues))
		{
			return true;
		}
		if (OutStep.RepeatCount > 1)
		{
			OutError = TEXT("a step that changes obstacles cannot repeat");
			return false;
		}
		OutStep.bChangesObstacles = true;
		return ParseObstacleList(*ObstacleValues, OutStep.Obstacles, OutError);
	}

	bool ParseStep(const FJsonObject& Object, FTDFog3DReplayStep& OutStep, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* KnobsObject = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* SphereValues = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* OperationValues = nullptr;
		bool bResets = false;
		if (!ReadNumber(Object, TEXT("dt"), OutStep.DeltaSeconds) || !ReadCellPoint(Object, TEXT("window_min"), OutStep.WindowMinCell)
			|| !ReadCellPoint(Object, TEXT("prev_window_min"), OutStep.PreviousWindowMinCell) || !Object.TryGetBoolField(TEXT("reset"), bResets)
			|| !Object.TryGetObjectField(TEXT("knobs"), KnobsObject) || !Object.TryGetArrayField(TEXT("spheres"), SphereValues)
			|| !Object.TryGetArrayField(TEXT("state_ops"), OperationValues))
		{
			OutError = TEXT("step is missing dt, window_min, prev_window_min, reset, knobs, spheres or state_ops");
			return false;
		}
		OutStep.bResets = bResets;
		if (!(*KnobsObject)->TryGetBoolField(TEXT("pressure"), OutStep.bAppliesPressure) || !(*KnobsObject)->TryGetBoolField(TEXT("relax"), OutStep.bAppliesRefill)
			|| !(*KnobsObject)->TryGetBoolField(TEXT("band"), OutStep.bAppliesEdgeBand))
		{
			OutError = TEXT("step knobs are missing pressure, relax or band");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& SphereValue : *SphereValues)
		{
			const TSharedPtr<FJsonObject>* SphereObject = nullptr;
			FTDFog3DReplaySphere Sphere;
			if (!SphereValue->TryGetObject(SphereObject) || !ParseSphere(**SphereObject, Sphere))
			{
				OutError = TEXT("sphere has missing fields");
				return false;
			}
			OutStep.Spheres.Add(Sphere);
		}

		for (const TSharedPtr<FJsonValue>& OperationValue : *OperationValues)
		{
			const TSharedPtr<FJsonObject>* OperationObject = nullptr;
			FString OperationName;
			const TSharedPtr<FJsonObject>* RegionObject = nullptr;
			FTDFog3DReplayStateOperation Operation;
			if (!OperationValue->TryGetObject(OperationObject) || !(*OperationObject)->TryGetStringField(TEXT("op"), OperationName)
				|| !(*OperationObject)->TryGetObjectField(TEXT("region"), RegionObject))
			{
				OutError = TEXT("state_ops item has missing fields");
				return false;
			}
			if (OperationName == TEXT("set_rest"))
			{
				Operation.Operation = TDFog3DReplayOperationRest;
			}
			else if (OperationName == TEXT("set_empty"))
			{
				Operation.Operation = TDFog3DReplayOperationEmpty;
			}
			else
			{
				OutError = FString::Printf(TEXT("unknown state op %s"), *OperationName);
				return false;
			}
			if (!ParseRegion(*RegionObject, 0, Operation.Region, OutError))
			{
				return false;
			}
			OutStep.Operations.Add(Operation);
		}

		if (Object.HasTypedField<EJson::Object>(TEXT("wind_override")) && !ParseWind(Object.GetObjectField(TEXT("wind_override")), OutStep.Wind, OutError))
		{
			return false;
		}

		if (Object.HasTypedField<EJson::String>(TEXT("dump")))
		{
			OutStep.DumpName = Object.GetStringField(TEXT("dump"));
		}

		const TArray<TSharedPtr<FJsonValue>>* FieldValues = nullptr;
		if (Object.TryGetArrayField(TEXT("dump_fields"), FieldValues))
		{
			for (const TSharedPtr<FJsonValue>& FieldValue : *FieldValues)
			{
				const int32 FieldIndex = FindReplayDumpFieldIndex(FieldValue->AsString());
				if (FieldIndex == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("unknown dump field %s"), *FieldValue->AsString());
					return false;
				}
				OutStep.DumpFieldIndices.Add(FieldIndex);
			}
		}
		if (OutStep.DumpFieldIndices.IsEmpty())
		{
			for (int32 FieldIndex = 0; FieldIndex < TDFog3DReplayDumpFieldCount; ++FieldIndex)
			{
				OutStep.DumpFieldIndices.Add(FieldIndex);
			}
		}

		int32 RepeatCount = 1;
		if (Object.HasField(TEXT("repeat")) && !ReadInteger(Object, TEXT("repeat"), RepeatCount))
		{
			OutError = TEXT("repeat is not a number");
			return false;
		}
		OutStep.RepeatCount = RepeatCount;
		if (RepeatCount < 1)
		{
			OutError = TEXT("repeat is below 1");
			return false;
		}
		return ParseStepObstacles(Object, OutStep, OutError);
	}

	bool ReadReplayWindBlend(const FJsonObject& Root, FTDFog3DReplay& OutReplay, FString& OutError)
	{
		if (!Root.HasField(TEXT("wind_blend")))
		{
			return true;
		}
		FString BlendName;
		const bool bHasBlendName = Root.TryGetStringField(TEXT("wind_blend"), BlendName);
		const bool bIsCurrentBlend = bHasBlendName && BlendName.Equals(ReplayCurrentWindBlendName, ESearchCase::CaseSensitive);
		const bool bIsClampedTimeBlend = bHasBlendName && BlendName.Equals(ReplayClampedTimeWindBlendName, ESearchCase::CaseSensitive);
		if (!bIsCurrentBlend && !bIsClampedTimeBlend)
		{
			OutError = FString::Printf(TEXT("wind_blend must be %s or %s"), ReplayCurrentWindBlendName, ReplayClampedTimeWindBlendName);
			return false;
		}
		OutReplay.bUsesClampedTimeWindBlend = bIsClampedTimeBlend;
		return true;
	}

	bool ReadReplayInputOcclusion(const FJsonObject& Root, FTDFog3DReplay& OutReplay, FString& OutError)
	{
		if (!Root.HasField(TEXT("input_occlusion")))
		{
			return true;
		}
		if (!Root.TryGetBoolField(TEXT("input_occlusion"), OutReplay.bOccludesInputBehindObstacles))
		{
			OutError = TEXT("input_occlusion must be true or false");
			return false;
		}
		return true;
	}

	bool IsReplayHeaderValid(const FJsonObject& Root, FTDFog3DReplay& OutReplay, FString& OutError)
	{
		FString FormatName;
		if (!Root.TryGetStringField(TEXT("format"), FormatName) || FormatName != ReplayFormatName)
		{
			OutError = FString::Printf(TEXT("unknown replay format %s"), *FormatName);
			return false;
		}

		const TSharedPtr<FJsonObject>* GridObject = nullptr;
		const TSharedPtr<FJsonObject>* TuningObject = nullptr;
		double LayerCount = 0.0;
		double CellSizeCm = 0.0;
		double LayerHeightCm = 0.0;
		if (!Root.TryGetStringField(TEXT("name"), OutReplay.Name) || !Root.TryGetObjectField(TEXT("grid"), GridObject) || !Root.TryGetObjectField(TEXT("tuning"), TuningObject)
			|| !ReadInteger(Root, TEXT("mode"), OutReplay.Mode) || !ReadCellPoint(Root, TEXT("anchor_cell"), OutReplay.AnchorCell)
			|| !ReadInteger(**GridObject, TEXT("n"), OutReplay.Resolution) || !ReadNumber(**GridObject, TEXT("layers"), LayerCount)
			|| !ReadNumber(**GridObject, TEXT("cell_cm"), CellSizeCm) || !ReadNumber(**GridObject, TEXT("layer_cm"), LayerHeightCm))
		{
			OutError = TEXT("replay is missing name, grid, tuning, mode or anchor_cell");
			return false;
		}
		if (static_cast<int32>(LayerCount) != ReplayLayerCount || CellSizeCm != ReplayCellSizeCm || LayerHeightCm != ReplayLayerHeightCm)
		{
			OutError = TEXT("grid must be 8 layers of 30 cm with 15.625 cm cells");
			return false;
		}
		if (OutReplay.Mode != 1 && OutReplay.Mode != 2)
		{
			OutError = TEXT("mode must be 1 or 2");
			return false;
		}
		if (!ParseTuning(**TuningObject, OutReplay.Tuning, OutReplay.Inflation))
		{
			OutError = TEXT("tuning has missing fields");
			return false;
		}
		return ReadReplayWindBlend(Root, OutReplay, OutError) && ReadReplayInputOcclusion(Root, OutReplay, OutError);
	}

	bool IsReplayBodyValid(const FJsonObject& Root, FTDFog3DReplay& OutReplay, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* ObstacleValues = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
		if (!Root.TryGetArrayField(TEXT("obstacles"), ObstacleValues) || !Root.TryGetArrayField(TEXT("steps"), StepValues) || StepValues->IsEmpty())
		{
			OutError = TEXT("replay is missing obstacles or steps");
			return false;
		}

		if (!ParseObstacleList(*ObstacleValues, OutReplay.Obstacles, OutError))
		{
			return false;
		}

		for (int32 StepIndex = 0; StepIndex < StepValues->Num(); ++StepIndex)
		{
			const TSharedPtr<FJsonObject>* StepObject = nullptr;
			FTDFog3DReplayStep Step;
			if (!(*StepValues)[StepIndex]->TryGetObject(StepObject) || !ParseStep(**StepObject, Step, OutError))
			{
				OutError = FString::Printf(TEXT("steps[%d]: %s"), StepIndex, *OutError);
				return false;
			}
			if (!OutReplay.bUsesClampedTimeWindBlend && HasReplayStepWindGain(Step))
			{
				OutError = FString::Printf(TEXT("steps[%d]: sphere wind gain g other than 1 needs wind_blend %s"), StepIndex, ReplayClampedTimeWindBlendName);
				return false;
			}
			OutReplay.ComputeCount += Step.RepeatCount;
			OutReplay.DumpCount += Step.DumpName.IsEmpty() ? 0 : 1;
			OutReplay.Steps.Add(MoveTemp(Step));
		}
		return true;
	}

	FString MakeReplaySha256Text(const uint8* Data, int64 ByteCount)
	{
		static const uint32 RoundConstants[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
		uint32 Hash[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

		TArray<uint8> Padded;
		Padded.Append(Data, static_cast<int32>(ByteCount));
		Padded.Add(0x80);
		while (Padded.Num() % 64 != 56)
		{
			Padded.Add(0);
		}
		const uint64 BitCount = static_cast<uint64>(ByteCount) * 8;
		for (int32 ShiftIndex = 7; ShiftIndex >= 0; --ShiftIndex)
		{
			Padded.Add(static_cast<uint8>(BitCount >> (ShiftIndex * 8)));
		}

		const auto RotateRight = [](uint32 Value, uint32 Count) { return (Value >> Count) | (Value << (32 - Count)); };
		for (int32 BlockStart = 0; BlockStart < Padded.Num(); BlockStart += 64)
		{
			uint32 Schedule[64];
			for (int32 WordIndex = 0; WordIndex < 16; ++WordIndex)
			{
				const uint8* Bytes = Padded.GetData() + BlockStart + WordIndex * 4;
				Schedule[WordIndex] = (static_cast<uint32>(Bytes[0]) << 24) | (static_cast<uint32>(Bytes[1]) << 16) | (static_cast<uint32>(Bytes[2]) << 8) | static_cast<uint32>(Bytes[3]);
			}
			for (int32 WordIndex = 16; WordIndex < 64; ++WordIndex)
			{
				const uint32 SmallSigmaZero = RotateRight(Schedule[WordIndex - 15], 7) ^ RotateRight(Schedule[WordIndex - 15], 18) ^ (Schedule[WordIndex - 15] >> 3);
				const uint32 SmallSigmaOne = RotateRight(Schedule[WordIndex - 2], 17) ^ RotateRight(Schedule[WordIndex - 2], 19) ^ (Schedule[WordIndex - 2] >> 10);
				Schedule[WordIndex] = Schedule[WordIndex - 16] + SmallSigmaZero + Schedule[WordIndex - 7] + SmallSigmaOne;
			}
			uint32 Work[8];
			FMemory::Memcpy(Work, Hash, sizeof(Work));
			for (int32 RoundIndex = 0; RoundIndex < 64; ++RoundIndex)
			{
				const uint32 BigSigmaOne = RotateRight(Work[4], 6) ^ RotateRight(Work[4], 11) ^ RotateRight(Work[4], 25);
				const uint32 Choice = (Work[4] & Work[5]) ^ (~Work[4] & Work[6]);
				const uint32 TemporaryOne = Work[7] + BigSigmaOne + Choice + RoundConstants[RoundIndex] + Schedule[RoundIndex];
				const uint32 BigSigmaZero = RotateRight(Work[0], 2) ^ RotateRight(Work[0], 13) ^ RotateRight(Work[0], 22);
				const uint32 Majority = (Work[0] & Work[1]) ^ (Work[0] & Work[2]) ^ (Work[1] & Work[2]);
				const uint32 TemporaryTwo = BigSigmaZero + Majority;
				Work[7] = Work[6];
				Work[6] = Work[5];
				Work[5] = Work[4];
				Work[4] = Work[3] + TemporaryOne;
				Work[3] = Work[2];
				Work[2] = Work[1];
				Work[1] = Work[0];
				Work[0] = TemporaryOne + TemporaryTwo;
			}
			for (int32 HashIndex = 0; HashIndex < 8; ++HashIndex)
			{
				Hash[HashIndex] += Work[HashIndex];
			}
		}

		FString Text;
		for (int32 HashIndex = 0; HashIndex < 8; ++HashIndex)
		{
			Text += FString::Printf(TEXT("%08x"), Hash[HashIndex]);
		}
		return Text;
	}

	bool DoesReplayGroundCoverSteps(const FTDFog3DReplay& Replay, const FIntPoint& SourceMinCell, const FIntPoint& SourceSize, FString& OutError)
	{
		const int32 Resolution = Replay.Resolution;
		for (int32 StepIndex = 0; StepIndex < Replay.Steps.Num(); ++StepIndex)
		{
			const FIntPoint WindowCells[2] = { Replay.Steps[StepIndex].WindowMinCell, Replay.Steps[StepIndex].PreviousWindowMinCell };
			for (const FIntPoint& WindowMinCell : WindowCells)
			{
				const bool bIsCovered = WindowMinCell.X - 1 >= SourceMinCell.X && WindowMinCell.Y - 1 >= SourceMinCell.Y
					&& WindowMinCell.X + Resolution <= SourceMinCell.X + SourceSize.X - 1 && WindowMinCell.Y + Resolution <= SourceMinCell.Y + SourceSize.Y - 1;
				if (!bIsCovered)
				{
					OutError = FString::Printf(TEXT("ground picture does not cover window plus ring at step %d (window %d,%d)"), StepIndex, WindowMinCell.X, WindowMinCell.Y);
					return false;
				}
			}
		}
		return true;
	}

	bool IsReplayGroundValid(const FJsonObject& Root, const FString& ReplayDirectory, FTDFog3DReplay& OutReplay, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* GroundObject = nullptr;
		if (!Root.TryGetObjectField(TEXT("ground"), GroundObject))
		{
			return true;
		}

		FIntPoint SourceMinCell;
		FIntPoint SourceSize;
		FString FileName;
		FString ExpectedSha256;
		double ReferenceZCm = 0.0;
		if (!ReadCellPoint(**GroundObject, TEXT("min_cell"), SourceMinCell) || !ReadCellPoint(**GroundObject, TEXT("size"), SourceSize)
			|| !(*GroundObject)->TryGetStringField(TEXT("file"), FileName) || !(*GroundObject)->TryGetStringField(TEXT("sha256"), ExpectedSha256)
			|| !ReadNumber(**GroundObject, TEXT("reference_z_cm"), ReferenceZCm))
		{
			OutError = TEXT("ground is missing min_cell, size, reference_z_cm, file or sha256");
			return false;
		}
		if (SourceSize.X < 1 || SourceSize.Y < 1 || SourceSize.X > 4096 || SourceSize.Y > 4096 || ReferenceZCm != 0.0)
		{
			OutError = TEXT("ground size is out of range or reference_z_cm is not 0");
			return false;
		}

		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(ReplayDirectory, FileName)))
		{
			OutError = FString::Printf(TEXT("cannot read ground file %s"), *FileName);
			return false;
		}
		if (Bytes.Num() != static_cast<int64>(SourceSize.X) * SourceSize.Y * static_cast<int64>(sizeof(float)))
		{
			OutError = FString::Printf(TEXT("ground file %s has %d bytes, expected %d x %d floats"), *FileName, Bytes.Num(), SourceSize.X, SourceSize.Y);
			return false;
		}
		if (MakeReplaySha256Text(Bytes.GetData(), Bytes.Num()) != ExpectedSha256.ToLower())
		{
			OutError = FString::Printf(TEXT("ground file %s sha256 differs from the replay"), *FileName);
			return false;
		}
		if (!DoesReplayGroundCoverSteps(OutReplay, SourceMinCell, SourceSize, OutError))
		{
			return false;
		}

		const int32 SquareSize = FMath::Max(SourceSize.X, SourceSize.Y);
		OutReplay.bHasGround = true;
		OutReplay.GroundSourceSize = SourceSize;
		OutReplay.GroundUpload.MinCell = SourceMinCell;
		OutReplay.GroundUpload.Size = FIntPoint(SquareSize, SquareSize);
		OutReplay.GroundUpload.HeightsCm.SetNumZeroed(SquareSize * SquareSize);
		const float* SourceHeights = reinterpret_cast<const float*>(Bytes.GetData());
		for (int32 RowIndex = 0; RowIndex < SourceSize.Y; ++RowIndex)
		{
			FMemory::Memcpy(OutReplay.GroundUpload.HeightsCm.GetData() + static_cast<int64>(RowIndex) * SquareSize, SourceHeights + static_cast<int64>(RowIndex) * SourceSize.X, SourceSize.X * sizeof(float));
		}
		return true;
	}

	int32 GetReplayCellCount(const FTDFog3DReplay& Replay)
	{
		return Replay.Resolution * Replay.Resolution * ReplayLayerCount;
	}

	int32 GetReplayWindowOrderIndex(const FTDFog3DReplay& Replay, int32 LocalX, int32 LocalY, int32 Layer)
	{
		return (LocalX * Replay.Resolution + LocalY) * ReplayLayerCount + Layer;
	}

	void FillReplayWindArray(const FTDFog3DReplay& Replay, const FTDFog3DReplayWind& Wind, TArray<FVector4f>& OutWindFaces)
	{
		const int32 Resolution = Replay.Resolution;
		OutWindFaces.SetNumZeroed(GetReplayCellCount(Replay));
		if (Wind.Kind == ETDFog3DReplayWindKind::Uniform)
		{
			const FVector4f Uniform(static_cast<float>(Wind.UniformCmPerSec.X), static_cast<float>(Wind.UniformCmPerSec.Y), static_cast<float>(Wind.UniformCmPerSec.Z), 0.0f);
			OutWindFaces.Init(Uniform, OutWindFaces.Num());
			return;
		}
		if (Wind.Kind == ETDFog3DReplayWindKind::VerticalOscillation)
		{
			const double Vertical = Wind.AmplitudeCmPerSec * FMath::Sin(2.0 * UE_DOUBLE_PI * Wind.TimeSeconds / Wind.PeriodSeconds);
			OutWindFaces.Init(FVector4f(0.0f, 0.0f, static_cast<float>(Vertical), 0.0f), OutWindFaces.Num());
			return;
		}

		const double Speed = Wind.VortexSpeedCmPerSec;
		const double HalfPeriod = Wind.VortexHalfPeriodCm;
		const double CenterOffsetCm = (Resolution / 2) * ReplayCellSizeCm;
		for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
		{
			const double FaceX = (LocalX + 1.0) * ReplayCellSizeCm - CenterOffsetCm;
			const double CenterX = (LocalX + 0.5) * ReplayCellSizeCm - CenterOffsetCm;
			const double SineFace = Speed * FMath::Sin(UE_DOUBLE_PI * FaceX / HalfPeriod);
			const double CosineCenter = -Speed * FMath::Cos(UE_DOUBLE_PI * CenterX / HalfPeriod);
			for (int32 Layer = 0; Layer < ReplayLayerCount; ++Layer)
			{
				const double LayerBottomCm = Layer * ReplayLayerHeightCm;
				const double LayerCenterCm = LayerBottomCm + ReplayHalfLayerHeightCm;
				const double LayerTopCm = LayerBottomCm + ReplayLayerHeightCm;
				const float HorizontalWind = static_cast<float>(Wind.VortexSign * (SineFace * FMath::Cos(UE_DOUBLE_PI * LayerCenterCm / HalfPeriod)));
				const float VerticalWind = static_cast<float>(Wind.VortexSign * (CosineCenter * FMath::Sin(UE_DOUBLE_PI * LayerTopCm / HalfPeriod)));
				for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
				{
					OutWindFaces[GetReplayWindowOrderIndex(Replay, LocalX, LocalY, Layer)] = FVector4f(HorizontalWind, 0.0f, VerticalWind, 0.0f);
				}
			}
		}
	}

	void FillReplayOperationArray(const FTDFog3DReplay& Replay, const FTDFog3DReplayStep& Step, TArray<uint8>& OutOperations)
	{
		const int32 Resolution = Replay.Resolution;
		OutOperations.SetNumZeroed(GetReplayCellCount(Replay));
		for (const FTDFog3DReplayStateOperation& Operation : Step.Operations)
		{
			for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
			{
				for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
				{
					for (int32 Layer = 0; Layer < ReplayLayerCount; ++Layer)
					{
						const FVector3d CenterCm = GetTDFog3DReplayCellCenterCm(Replay, Step.WindowMinCell, LocalX, LocalY, Layer);
						if (IsTDFog3DReplayRegionContaining(*Operation.Region, CenterCm, FIntVector(LocalX, LocalY, Layer)))
						{
							OutOperations[GetReplayWindowOrderIndex(Replay, LocalX, LocalY, Layer)] = Operation.Operation;
						}
					}
				}
			}
		}
	}

	FVector3d ComputeReplayBlendVelocityCmPerSec(const FTDFog3DReplay& Replay, const FTDFog3DReplaySphere& Sphere)
	{
		if (!Replay.bUsesClampedTimeWindBlend)
		{
			return Sphere.VelocityCmPerSec;
		}
		return Sphere.VelocityCmPerSec * Sphere.WindGain;
	}
}

FString GetTDFog3DReplayDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/replays")));
}

FString GetTDFog3DDumpDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/Fog3D/gpu")));
}

FString GetTDFog3DAddendumReplayDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/replays_add1")));
}

FString GetTDFog3DAddendumDumpDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/Fog3D/gpu_add1")));
}

FString GetTDFog3DStage1dReplayDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/replays_v2")));
}

FString GetTDFog3DStage1dDumpDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/Fog3D/gpu_1d")));
}

FString GetTDFog3DStage2aReplayDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/replays_s2a/R5")));
}

FString GetTDFog3DStage2aDumpDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/Fog3D/gpu_s2a_r5")));
}

const TArray<FTDFog3DReplayObstacle>& FindTDFog3DReplayActiveObstacles(const FTDFog3DReplay& Replay, int32 StepIndex)
{
	for (int32 Index = FMath::Min(StepIndex, Replay.Steps.Num() - 1); Index >= 0; --Index)
	{
		if (Replay.Steps[Index].bChangesObstacles)
		{
			return Replay.Steps[Index].Obstacles;
		}
	}
	return Replay.Obstacles;
}

bool LoadTDFog3DReplay(const FString& FilePath, FTDFog3DReplay& OutReplay, FString& OutError)
{
	using namespace TDFluidGpuFog3DReplayPrivate;

	OutReplay = FTDFog3DReplay();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("cannot parse %s"), *FilePath);
		return false;
	}

	if (!IsReplayHeaderValid(*Root, OutReplay, OutError) || !IsReplayBodyValid(*Root, OutReplay, OutError) || !IsReplayGroundValid(*Root, FPaths::GetPath(FilePath), OutReplay, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(FilePath), *OutError);
		return false;
	}
	OutReplay.CellSizeCm = static_cast<float>(ReplayCellSizeCm);
	OutReplay.LayerHeightCm = static_cast<float>(ReplayLayerHeightCm);
	return true;
}

bool LoadTDFog3DReplayManifestNames(const FString& Directory, TArray<FString>& OutReplayFileNames, FString& OutError)
{
	OutReplayFileNames.Reset();
	const FString ManifestPath = FPaths::Combine(Directory, TEXT("manifest.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("files"), Files))
	{
		OutError = FString::Printf(TEXT("cannot parse the file list of %s"), *ManifestPath);
		return false;
	}

	for (const TSharedPtr<FJsonValue>& File : *Files)
	{
		const TSharedPtr<FJsonObject>* FileObject = nullptr;
		FString Name;
		if (!File.IsValid() || !File->TryGetObject(FileObject) || !(*FileObject)->TryGetStringField(TEXT("name"), Name))
		{
			OutError = FString::Printf(TEXT("%s lists a file without a name"), *ManifestPath);
			return false;
		}
		if (Name.EndsWith(TEXT(".json")))
		{
			OutReplayFileNames.Add(Name);
		}
	}
	OutReplayFileNames.Sort();
	return true;
}

FVector3d GetTDFog3DReplayWindowOffsetCm(const FTDFog3DReplay& Replay, const FIntPoint& WindowMinCell)
{
	const int64 OffsetCellsX = static_cast<int64>(Replay.AnchorCell.X) - WindowMinCell.X;
	const int64 OffsetCellsY = static_cast<int64>(Replay.AnchorCell.Y) - WindowMinCell.Y;
	return FVector3d(OffsetCellsX * TDFluidGpuFog3DReplayPrivate::ReplayCellSizeCm, OffsetCellsY * TDFluidGpuFog3DReplayPrivate::ReplayCellSizeCm, 0.0);
}

FVector3d GetTDFog3DReplayCellCenterCm(const FTDFog3DReplay& Replay, const FIntPoint& WindowMinCell, int32 LocalX, int32 LocalY, int32 Layer)
{
	using namespace TDFluidGpuFog3DReplayPrivate;

	const int64 CellsFromAnchorX = static_cast<int64>(WindowMinCell.X) - Replay.AnchorCell.X + LocalX;
	const int64 CellsFromAnchorY = static_cast<int64>(WindowMinCell.Y) - Replay.AnchorCell.Y + LocalY;
	return FVector3d((CellsFromAnchorX + 0.5) * ReplayCellSizeCm, (CellsFromAnchorY + 0.5) * ReplayCellSizeCm, Layer * ReplayLayerHeightCm + ReplayHalfLayerHeightCm);
}

bool DoesTDFog3DReplayStepMoveWindow(const FTDFog3DReplayStep& Step)
{
	return Step.WindowMinCell != Step.PreviousWindowMinCell;
}

bool IsTDFog3DReplayRegionContaining(const FTDFog3DReplayRegion& Region, const FVector3d& CenterCm, const FIntVector& LocalCell)
{
	switch (Region.Kind)
	{
	case ETDFog3DReplayRegionKind::All:
		return true;
	case ETDFog3DReplayRegionKind::Aabb:
		return CenterCm.X >= Region.MinimumCm.X && CenterCm.X <= Region.MaximumCm.X && CenterCm.Y >= Region.MinimumCm.Y && CenterCm.Y <= Region.MaximumCm.Y
			&& CenterCm.Z >= Region.MinimumCm.Z && CenterCm.Z <= Region.MaximumCm.Z;
	case ETDFog3DReplayRegionKind::Halfspace:
	case ETDFog3DReplayRegionKind::Slab:
	{
		const double SignedDistance = (CenterCm.X - Region.PointCm.X) * Region.Normal.X + (CenterCm.Y - Region.PointCm.Y) * Region.Normal.Y + (CenterCm.Z - Region.PointCm.Z) * Region.Normal.Z;
		const bool bIsInsideSide = Region.Kind == ETDFog3DReplayRegionKind::Halfspace ? SignedDistance > 0.0 : (SignedDistance > Region.DistanceMinimumCm && SignedDistance <= Region.DistanceMaximumCm);
		return bIsInsideSide && CenterCm.Z >= Region.HeightMinimumCm && CenterCm.Z <= Region.HeightMaximumCm;
	}
	case ETDFog3DReplayRegionKind::CellBox:
		return LocalCell.X >= Region.CellMinimum.X && LocalCell.X <= Region.CellMaximum.X && LocalCell.Y >= Region.CellMinimum.Y && LocalCell.Y <= Region.CellMaximum.Y
			&& LocalCell.Z >= Region.CellMinimum.Z && LocalCell.Z <= Region.CellMaximum.Z;
	case ETDFog3DReplayRegionKind::Not:
		return !Region.Children.IsEmpty() && !IsTDFog3DReplayRegionContaining(*Region.Children[0], CenterCm, LocalCell);
	case ETDFog3DReplayRegionKind::Union:
		for (const TSharedPtr<FTDFog3DReplayRegion>& Child : Region.Children)
		{
			if (IsTDFog3DReplayRegionContaining(*Child, CenterCm, LocalCell))
			{
				return true;
			}
		}
		return false;
	}
	return false;
}

FTDFluidGpuFog3DFrameInput MakeTDFog3DReplayFrameInput(const FTDFog3DReplay& Replay, int32 StepIndex, int32 RepeatIndex)
{
	const FTDFog3DReplayStep& Step = Replay.Steps[StepIndex];
	const bool bIsFirstRepeat = RepeatIndex == 0;
	const FVector3d WindowOffsetCm = GetTDFog3DReplayWindowOffsetCm(Replay, Step.WindowMinCell);

	FTDFluidGpuFog3DFrameInput FrameInput;
	FrameInput.Resolution = Replay.Resolution;
	FrameInput.CellSizeCm = Replay.CellSizeCm;
	FrameInput.LayerHeightCm = Replay.LayerHeightCm;
	FrameInput.Mode = Replay.Mode;
	FrameInput.WindowMinCell = Step.WindowMinCell;
	FrameInput.PreviousWindowMinCell = bIsFirstRepeat ? Step.PreviousWindowMinCell : Step.WindowMinCell;
	FrameInput.bResetsState = bIsFirstRepeat && Step.bResets;
	FrameInput.bObstaclesChanged = bIsFirstRepeat && Step.bChangesObstacles;
	FrameInput.DeltaSeconds = static_cast<float>(Step.DeltaSeconds);
	FrameInput.bUsesClampedTimeWindBlend = Replay.bUsesClampedTimeWindBlend;
	FrameInput.bOccludesInputBehindObstacles = Replay.bOccludesInputBehindObstacles;
	FrameInput.Tuning = Replay.Tuning;
	FrameInput.TestKnobs.bAppliesPressure = Step.bAppliesPressure;
	FrameInput.TestKnobs.bAppliesRefill = Step.bAppliesRefill;
	FrameInput.TestKnobs.bAppliesEdgeBand = Step.bAppliesEdgeBand;
	if (Replay.bHasGround)
	{
		const bool bIsFirstCompute = StepIndex == 0 && bIsFirstRepeat;
		FrameInput.bFollowsGround = true;
		FrameInput.bGroundChanged = bIsFirstCompute;
		FrameInput.GroundUpload = bIsFirstCompute ? &Replay.GroundUpload : nullptr;
	}

	for (const FTDFog3DReplaySphere& Sphere : Step.Spheres)
	{
		const FVector3d StartCm = Sphere.StartCm + WindowOffsetCm;
		const FVector3d EndCm = Sphere.EndCm + WindowOffsetCm;
		FTDFluidGpuFog3DSphere& Converted = FrameInput.Spheres.AddDefaulted_GetRef();
		Converted.StartCmRadiusCm = FVector4f(static_cast<float>(StartCm.X), static_cast<float>(StartCm.Y), static_cast<float>(StartCm.Z), static_cast<float>(Sphere.RadiusCm));
		Converted.EndCmWindStrength = FVector4f(static_cast<float>(EndCm.X), static_cast<float>(EndCm.Y), static_cast<float>(EndCm.Z), static_cast<float>(Sphere.WindStrength));
		const FVector3d VelocityCmPerSec = TDFluidGpuFog3DReplayPrivate::ComputeReplayBlendVelocityCmPerSec(Replay, Sphere);
		Converted.VelocityCmPerSecErosionPerSec = FVector4f(
			static_cast<float>(VelocityCmPerSec.X), static_cast<float>(VelocityCmPerSec.Y), static_cast<float>(VelocityCmPerSec.Z), static_cast<float>(Sphere.ErosionPerSec));
		FrameInput.SphereAnchors.Add(Sphere.bHasOcclusionAnchor
			? MakeTDFluidGpuFog3DSphereAnchor(Sphere.OcclusionAnchorCm + WindowOffsetCm, TDFluidGpuFog3DReplayPrivate::ReplayCellSizeCm, Replay.Resolution)
			: FTDFluidGpuFog3DSphereAnchor());
	}

	for (const FTDFog3DReplayObstacle& Obstacle : FindTDFog3DReplayActiveObstacles(Replay, StepIndex))
	{
		FrameInput.Obstacles.Add(MakeTDFluidGpuFog3DUprightBox(Obstacle.CenterCm + WindowOffsetCm, Obstacle.HalfExtentCm, Obstacle.YawDegrees, Replay.Inflation));
	}
	return FrameInput;
}

bool MakeTDFog3DReplayStateOverride(const FTDFog3DReplay& Replay, int32 StepIndex, int32 RepeatIndex, FTDFluidGpuFog3DStateOverride& OutStateOverride)
{
	using namespace TDFluidGpuFog3DReplayPrivate;

	const FTDFog3DReplayStep& Step = Replay.Steps[StepIndex];
	const bool bHasWind = Step.Wind.Kind != ETDFog3DReplayWindKind::None;
	const bool bHasOperations = RepeatIndex == 0 && !Step.Operations.IsEmpty();
	if (!bHasWind && !bHasOperations)
	{
		return false;
	}

	OutStateOverride = FTDFluidGpuFog3DStateOverride();
	if (bHasWind)
	{
		FillReplayWindArray(Replay, Step.Wind, OutStateOverride.WindFaces);
	}
	if (bHasOperations)
	{
		FillReplayOperationArray(Replay, Step, OutStateOverride.FillOperations);
	}
	return true;
}

#endif
