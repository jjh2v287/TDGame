#include "FluidFX/TDFog3DLayer.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/TextureRenderTargetVolume.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

struct FTDFog3DLayerRhiTestAccess
{
	static TSharedPtr<FTDFog3DRenderSlot, ESPMode::ThreadSafe> GetRenderSlot(const FTDFog3DLayer& Layer)
	{
		return Layer.RenderSlot;
	}
};

namespace TDFog3DLayerRhiTestPrivate
{
	constexpr int32 LayerResolution = 128;
	constexpr int32 PhaseOneFrameCount = 120;
	constexpr int32 TotalFrameCount = 140;
	constexpr int32 SmashFirstFrame = 30;
	constexpr int32 SmashLastFrame = 32;
	constexpr int32 HitCheckLastFrame = 50;
	constexpr int32 SlashFirstFrame = 60;
	constexpr int32 SlashLastFrame = 80;
	constexpr int32 TeleportFrame = 100;
	constexpr int32 TeleportInputLastFrame = 104;
	constexpr int32 FarCellDistance = 30;
	constexpr int32 DumpFieldDisplacementX = 3;
	constexpr int32 MaximumReadbackExtraSubmitCount = 16;
	constexpr double PlayerStartX = -100.0;
	constexpr double PlayerSpeedCmPerSec = 400.0;
	constexpr double TeleportDistanceCm = 10000.0;
	constexpr double PlayerHeightCm = 100.0;
	constexpr double FloorZ = 0.0;
	constexpr float PhaseOneFrameSeconds = 1.0f / 60.0f;
	constexpr float PhaseTwoFrameSeconds = 1.0f / 120.0f;
	constexpr double HitFillLimit = -0.05;
	constexpr double FillLowerLimit = -1.0 - 1.0e-3;
	constexpr double FillUpperLimit = 1.0e-3;
	constexpr double DisplacementLimit = 500.0;
	constexpr double HalfFloatRelativeTolerance = 1.0e-3;
	constexpr double HalfFloatAbsoluteTolerance = 1.0e-6;
	constexpr int32 TeleportHandSlot = 100;

	struct FDumpCapture
	{
		TArray<float> Values;
		FIntPoint WindowMinCell = FIntPoint::ZeroValue;
		int32 Resolution = 0;
		bool bHasStepped = false;
		bool bCopied = false;
		int32 RejectedStepCount = -1;
	};

	struct FRunResult
	{
		TArray<uint32> FrameCrcs;
		int32 SteppedFrameCount = 0;
		int32 SkippedFrameCount = 0;
		int32 PhaseOneUnsteppedFrameCount = 0;
		int32 SubmitRefusedCount = 0;
		int32 SkippedSubmitAcceptedCount = 0;
		int32 ReadFailureCount = 0;
		int32 NonFiniteCount = 0;
		int32 FillOutOfRangeCount = 0;
		int32 DisplacementOutOfRangeCount = 0;
		int32 OutputStateMismatchCount = 0;
		double OutputStateMaximumError = 0.0;
		int32 StateWindowMismatchCount = 0;
		int32 SkippedOutputChangedCount = 0;
		int32 SkippedWindowChangedCount = 0;
		int32 EarlyNonZeroCount = 0;
		int32 WindowMoveCount = 0;
		int32 WindowMoveAfterHitCount = 0;
		int32 HitCheckCount = 0;
		int32 HitCheckFailureCount = 0;
		int32 HitCheckAfterMoveCount = 0;
		double HitFillAtStrike = 0.0;
		double HitFillMaximum = -2.0;
		FString HitFillTrace;
		int32 FarCheckedCellCount = 0;
		int32 FarNonZeroCount = 0;
		int32 TeleportCheckedCellCount = 0;
		int32 TeleportOutsideNonZeroCount = 0;
		int32 TeleportInsideNonZeroCount = 0;
		bool bTeleportWindowJumped = false;
		bool bHitWasRecorded = false;
		int32 SeenRejectedStepCount = 0;
		int32 SlotRejectedStepCount = -1;
		uint32 SubmittedStepCount = 0;
		int32 DumpFailureCount = 0;
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	UTextureRenderTargetVolume* CreateLayerOutputTarget()
	{
		UTextureRenderTargetVolume* Target = NewObject<UTextureRenderTargetVolume>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bSupportsUAV = true;
		Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		Target->Init(LayerResolution, LayerResolution, TDFluidGpuFog3DLayerCount, PF_FloatRGBA);
		Target->UpdateResourceImmediate(true);
		FlushRenderingCommands();
		return Target;
	}

	FTDFog3DSettings MakeLayerRhiSettings()
	{
		FTDFog3DSettings Settings;
		Settings.QualityLowStepsPerSecond = 60.0f;
		Settings.BladeRadiusCm = 20.0f;
		Settings.BladeWindStrength = 1.0f;
		return Settings;
	}

	FTDFog3DInputSample MakeInputSample(int32 SlotId, const FVector& World, float RadiusCm, float WindStrength, float ErosionPerSec)
	{
		FTDFog3DInputSample Sample;
		Sample.SlotId = SlotId;
		Sample.CurrentWorld = World;
		Sample.RadiusCm = RadiusCm;
		Sample.WindStrength = WindStrength;
		Sample.ErosionPerSec = ErosionPerSec;
		return Sample;
	}

	void AddBladeInputs(const FTDFog3DSettings& Settings, const FVector& BaseWorld, const FVector& TipWorld, TArray<FTDFog3DInputSample>& OutInputs, FVector& OutTipCenter)
	{
		TArray<FVector> Centers;
		FTDFog3DLayer::ComputeBladeSphereCenters(BaseWorld, TipWorld, Settings.BladeRadiusCm, Centers);
		for (int32 SlotId = 0; SlotId < Centers.Num(); ++SlotId)
		{
			OutInputs.Add(MakeInputSample(SlotId, Centers[SlotId], Settings.BladeRadiusCm, Settings.BladeWindStrength, Settings.BladeErosionPerSec));
		}
		OutTipCenter = Centers.IsEmpty() ? TipWorld : Centers[0];
	}

	FVector MakePlayerWorld(int32 Frame, double ElapsedSeconds)
	{
		const double TeleportOffset = Frame >= TeleportFrame ? TeleportDistanceCm : 0.0;
		return FVector(PlayerStartX + PlayerSpeedCmPerSec * ElapsedSeconds + TeleportOffset, 0.0, PlayerHeightCm);
	}

	void MakeFrameInputs(int32 Frame, const FVector& Player, const FTDFog3DSettings& Settings, TArray<FTDFog3DInputSample>& OutInputs, FVector& OutSmashTip, FVector& OutHand)
	{
		if (Frame >= SmashFirstFrame && Frame <= SmashLastFrame)
		{
			const double TipZ = 104.0 - 50.0 * (Frame - SmashFirstFrame);
			const FVector Tip(Player.X + 80.0, Player.Y, TipZ);
			const FVector Base(Player.X + 80.0, Player.Y, TipZ + 96.0);
			AddBladeInputs(Settings, Base, Tip, OutInputs, OutSmashTip);
			return;
		}

		if (Frame >= SlashFirstFrame && Frame <= SlashLastFrame)
		{
			const double SlashY = -150.0 + 15.0 * (Frame - SlashFirstFrame);
			const FVector Base(Player.X + 20.0, SlashY, 60.0);
			const FVector Tip(Player.X + 116.0, SlashY, 60.0);
			FVector IgnoredTip;
			AddBladeInputs(Settings, Base, Tip, OutInputs, IgnoredTip);
			return;
		}

		if (Frame >= TeleportFrame && Frame <= TeleportInputLastFrame)
		{
			OutHand = FVector(Player.X + 60.0, Player.Y, 60.0);
			OutInputs.Add(MakeInputSample(TeleportHandSlot, OutHand, Settings.HandRadiusCm, Settings.BodyWindStrength, Settings.BladeErosionPerSec));
		}
	}

	bool ReadOutputLayers(FTextureRenderTargetResource& Resource, TArray<TArray<FFloat16Color>>& OutLayers)
	{
		OutLayers.SetNum(TDFluidGpuFog3DLayerCount);
		for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
		{
			FReadSurfaceDataFlags ReadFlags;
			ReadFlags.SetArrayIndex(Layer);
			const bool bIsRead = Resource.ReadFloat16Pixels(OutLayers[Layer], ReadFlags, FIntRect(0, 0, LayerResolution, LayerResolution));
			if (!bIsRead || OutLayers[Layer].Num() != LayerResolution * LayerResolution)
			{
				return false;
			}
		}
		return true;
	}

	uint32 ComputeOutputCrc(const TArray<TArray<FFloat16Color>>& Layers)
	{
		uint32 Crc = 0;
		for (const TArray<FFloat16Color>& Layer : Layers)
		{
			Crc = FCrc::MemCrc32(Layer.GetData(), Layer.Num() * sizeof(FFloat16Color), Crc);
		}
		return Crc;
	}

	double ReadChannel(const TArray<TArray<FFloat16Color>>& Layers, int32 LocalX, int32 LocalY, int32 Layer, int32 Channel)
	{
		const FFloat16Color& Texel = Layers[Layer][LocalY * LayerResolution + LocalX];
		switch (Channel)
		{
		case 0:
			return Texel.R.GetFloat();
		case 1:
			return Texel.G.GetFloat();
		case 2:
			return Texel.B.GetFloat();
		default:
			return Texel.A.GetFloat();
		}
	}

	bool IsTexelZero(const TArray<TArray<FFloat16Color>>& Layers, int32 LocalX, int32 LocalY, int32 Layer)
	{
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			if (ReadChannel(Layers, LocalX, LocalY, Layer, Channel) != 0.0)
			{
				return false;
			}
		}
		return true;
	}

	int32 GetDumpCellIndex(int32 LocalX, int32 LocalY, int32 Layer)
	{
		return (LocalX * LayerResolution + LocalY) * TDFluidGpuFog3DLayerCount + Layer;
	}

	void CaptureSlotDump(const TSharedPtr<FTDFog3DRenderSlot, ESPMode::ThreadSafe>& Slot, const TSharedRef<FDumpCapture, ESPMode::ThreadSafe>& Capture)
	{
		ENQUEUE_RENDER_COMMAND(TDFog3DLayerRhiDump)([Slot, Capture](FRHICommandListImmediate& RHICmdList)
		{
			Capture->bCopied = false;
			Capture->WindowMinCell = Slot->State.WindowMinCell;
			Capture->Resolution = Slot->State.Resolution;
			Capture->bHasStepped = Slot->State.bHasStepped;
			Capture->RejectedStepCount = Slot->RejectedStepCount.load();
			FRHIGPUBufferReadback Readback(TEXT("TDGame.Fog3DLayerRhiDump"));
			bool bIsAccepted = false;
			{
				FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFog3DLayerRhiDump"));
				bIsAccepted = AddTDFluidGpuFog3DDumpPass(GraphBuilder, Slot->State, Readback);
				GraphBuilder.Execute();
			}
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			if (!bIsAccepted)
			{
				return;
			}
			int32 ExtraSubmitCount = 0;
			while (!Readback.IsReady() && ExtraSubmitCount < MaximumReadbackExtraSubmitCount)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				++ExtraSubmitCount;
			}
			Capture->bCopied = CopyTDFluidGpuFog3DDump(Readback, Slot->State.Resolution, Capture->Values);
		});
		FlushRenderingCommands();
	}

	void CompareOutputWithState(const TArray<TArray<FFloat16Color>>& Layers, const FDumpCapture& Dump, FRunResult& Result)
	{
		const int32 CellCount = LayerResolution * LayerResolution * TDFluidGpuFog3DLayerCount;
		for (int32 LocalX = 0; LocalX < LayerResolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < LayerResolution; ++LocalY)
			{
				for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
				{
					const int32 CellIndex = GetDumpCellIndex(LocalX, LocalY, Layer);
					bool bMatches = true;
					for (int32 Channel = 0; Channel < 4; ++Channel)
					{
						const double OutputValue = ReadChannel(Layers, LocalX, LocalY, Layer, Channel);
						const double StateValue = Dump.Values[CellCount * (DumpFieldDisplacementX + Channel) + CellIndex];
						const double Error = FMath::Abs(OutputValue - StateValue);
						Result.OutputStateMaximumError = FMath::Max(Result.OutputStateMaximumError, Error);
						bMatches &= FMath::IsFinite(OutputValue) && Error <= HalfFloatRelativeTolerance * FMath::Abs(StateValue) + HalfFloatAbsoluteTolerance;
						if (!FMath::IsFinite(OutputValue))
						{
							++Result.NonFiniteCount;
							continue;
						}
						if (Channel == 3)
						{
							Result.FillOutOfRangeCount += (OutputValue < FillLowerLimit || OutputValue > FillUpperLimit) ? 1 : 0;
							continue;
						}
						Result.DisplacementOutOfRangeCount += FMath::Abs(OutputValue) > DisplacementLimit ? 1 : 0;
					}
					Result.OutputStateMismatchCount += bMatches ? 0 : 1;
				}
			}
		}
	}

	int32 CountNonZeroTexels(const TArray<TArray<FFloat16Color>>& Layers)
	{
		int32 NonZeroCount = 0;
		for (int32 LocalX = 0; LocalX < LayerResolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < LayerResolution; ++LocalY)
			{
				for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
				{
					NonZeroCount += IsTexelZero(Layers, LocalX, LocalY, Layer) ? 0 : 1;
				}
			}
		}
		return NonZeroCount;
	}

	void CheckFarCellsAreZero(const TArray<TArray<FFloat16Color>>& Layers, const FIntPoint& HitLocalCell, FRunResult& Result)
	{
		for (int32 LocalX = 0; LocalX < LayerResolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < LayerResolution; ++LocalY)
			{
				const int32 CellDistance = FMath::Max(FMath::Abs(LocalX - HitLocalCell.X), FMath::Abs(LocalY - HitLocalCell.Y));
				if (CellDistance <= FarCellDistance)
				{
					continue;
				}
				for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
				{
					++Result.FarCheckedCellCount;
					Result.FarNonZeroCount += IsTexelZero(Layers, LocalX, LocalY, Layer) ? 0 : 1;
				}
			}
		}
	}

	void CheckTeleportCells(const TArray<TArray<FFloat16Color>>& Layers, const FIntPoint& WindowMinCell, const FVector& HandWorld, const FTDFog3DSettings& Settings, FRunResult& Result)
	{
		const double ReachCm = Settings.HandRadiusCm + 2.0 * Settings.CellSizeCm;
		for (int32 LocalX = 0; LocalX < LayerResolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < LayerResolution; ++LocalY)
			{
				for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
				{
					const FVector CellCenter(
						(WindowMinCell.X + LocalX + 0.5) * Settings.CellSizeCm,
						(WindowMinCell.Y + LocalY + 0.5) * Settings.CellSizeCm,
						FloorZ + (Layer + 0.5) * Settings.LayerThicknessCm);
					const bool bIsOutside = FVector::Dist(CellCenter, HandWorld) > ReachCm;
					const bool bIsZero = IsTexelZero(Layers, LocalX, LocalY, Layer);
					if (bIsOutside)
					{
						++Result.TeleportCheckedCellCount;
						Result.TeleportOutsideNonZeroCount += bIsZero ? 0 : 1;
						continue;
					}
					Result.TeleportInsideNonZeroCount += bIsZero ? 0 : 1;
				}
			}
		}
	}

	UTextureRenderTargetVolume* CreateMalformedOutputTarget(bool bSupportsUAV, EPixelFormat Format)
	{
		UTextureRenderTargetVolume* Target = NewObject<UTextureRenderTargetVolume>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bSupportsUAV = bSupportsUAV;
		Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		Target->Init(LayerResolution, LayerResolution, TDFluidGpuFog3DLayerCount, Format);
		Target->UpdateResourceImmediate(true);
		FlushRenderingCommands();
		return Target;
	}

	void VerifyMalformedOutputTargetRefused(FAutomationTestBase& Test, const TCHAR* Label, bool bSupportsUAV, EPixelFormat Format)
	{
		TStrongObjectPtr<UTextureRenderTargetVolume> Target(CreateMalformedOutputTarget(bSupportsUAV, Format));
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: malformed output target exists"), Label), Target.Get()))
		{
			return;
		}
		Test.TestNotNull(*FString::Printf(TEXT("%s: malformed output target has a resource"), Label), Target->GameThread_GetRenderTargetResource());
		Test.TestEqual(*FString::Printf(TEXT("%s: malformed output target has the layer size"), Label), Target->SizeX, LayerResolution);

		FTDFog3DLayer Layer;
		Layer.Configure(MakeLayerRhiSettings(), 0, 2);
		FTDFog3DStepRequest Request;
		Request.PlayerWorld = FVector(0.0, 0.0, PlayerHeightCm);
		Request.FloorZ = FloorZ;
		Request.Clock = Layer.AdvanceClock(PhaseOneFrameSeconds);
		Test.TestTrue(*FString::Printf(TEXT("%s: the clock asks for a step"), Label), Request.Clock.bStepsNow);
		Test.TestFalse(*FString::Printf(TEXT("%s: a malformed output target is refused"), Label), Layer.SubmitStep(Request, *Target));
		Test.TestEqual(*FString::Printf(TEXT("%s: no step is submitted"), Label), static_cast<int32>(Layer.GetSubmittedStepCount()), 0);
		const TSharedPtr<FTDFog3DRenderSlot, ESPMode::ThreadSafe> Slot = FTDFog3DLayerRhiTestAccess::GetRenderSlot(Layer);
		Test.TestEqual(*FString::Printf(TEXT("%s: the render slot rejects nothing"), Label), Slot.IsValid() ? Slot->RejectedStepCount.load() : 0, 0);
		Layer.Release();
		FlushRenderingCommands();
	}

	bool RunLayerScenario(FAutomationTestBase& Test, const FString& RunLabel, FRunResult& Result)
	{
		const FTDFog3DSettings Settings = MakeLayerRhiSettings();
		TStrongObjectPtr<UTextureRenderTargetVolume> Target(CreateLayerOutputTarget());
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: output target exists"), *RunLabel), Target.Get()))
		{
			return false;
		}
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: output target has a resource"), *RunLabel), Resource))
		{
			return false;
		}

		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 0, 2);
		if (!Test.TestEqual(*FString::Printf(TEXT("%s: low quality resolution"), *RunLabel), Layer.GetResolution(), LayerResolution))
		{
			return false;
		}
		Test.TestNearlyEqual(*FString::Printf(TEXT("%s: steps per second"), *RunLabel), Layer.GetStepsPerSecond(), 60.0f, 1.0e-4f);

		double ElapsedSeconds = 0.0;
		uint32 PreviousCrc = 0;
		FIntPoint PreviousWindow = FIntPoint::ZeroValue;
		FIntPoint HitWorldCell = FIntPoint::ZeroValue;
		FIntPoint WindowAtStrike = FIntPoint::ZeroValue;
		FVector HandWorld = FVector::ZeroVector;
		bool bHasPreviousFrame = false;
		bool bFirstStrikeWindowSet = false;
		TArray<TArray<FFloat16Color>> Layers;

		for (int32 Frame = 0; Frame < TotalFrameCount; ++Frame)
		{
			const float FrameSeconds = Frame < PhaseOneFrameCount ? PhaseOneFrameSeconds : PhaseTwoFrameSeconds;
			ElapsedSeconds += FrameSeconds;
			const FVector Player = MakePlayerWorld(Frame, ElapsedSeconds);
			const FTDFog3DClockDecision Clock = Layer.AdvanceClock(FrameSeconds);

			FTDFog3DStepRequest Request;
			Request.PlayerWorld = Player;
			Request.FloorZ = FloorZ;
			Request.Clock = Clock;
			FVector SmashTip = FVector::ZeroVector;
			MakeFrameInputs(Frame, Player, Settings, Request.Inputs, SmashTip, HandWorld);

			const bool bStepped = Clock.bStepsNow;
			Result.PhaseOneUnsteppedFrameCount += (Frame < PhaseOneFrameCount && !bStepped) ? 1 : 0;
			const bool bAccepted = Layer.SubmitStep(Request, *Target);
			if (bStepped)
			{
				++Result.SteppedFrameCount;
				Result.SubmitRefusedCount += bAccepted ? 0 : 1;
			}
			else
			{
				++Result.SkippedFrameCount;
				Result.SkippedSubmitAcceptedCount += bAccepted ? 1 : 0;
			}

			FlushRenderingCommands();
			const FIntPoint Window = Layer.GetSubmittedWindowMinCell();
			const TSharedPtr<FTDFog3DRenderSlot, ESPMode::ThreadSafe> Slot = FTDFog3DLayerRhiTestAccess::GetRenderSlot(Layer);
			if (!Slot.IsValid() || !ReadOutputLayers(*Resource, Layers))
			{
				++Result.ReadFailureCount;
				continue;
			}

			const TSharedRef<FDumpCapture, ESPMode::ThreadSafe> Dump = MakeShared<FDumpCapture, ESPMode::ThreadSafe>();
			CaptureSlotDump(Slot, Dump);
			const int32 ExpectedDumpValueCount = LayerResolution * LayerResolution * TDFluidGpuFog3DLayerCount * TDFluidGpuFog3DDumpFieldCount;
			if (!Dump->bCopied || Dump->Values.Num() != ExpectedDumpValueCount || Dump->Resolution != LayerResolution)
			{
				++Result.DumpFailureCount;
				continue;
			}

			const uint32 Crc = ComputeOutputCrc(Layers);
			Result.FrameCrcs.Add(Crc);
			CompareOutputWithState(Layers, *Dump, Result);
			Result.StateWindowMismatchCount += (Dump->WindowMinCell != Window || !Dump->bHasStepped) ? 1 : 0;
			Result.SlotRejectedStepCount = Dump->RejectedStepCount;

			if (Frame < SmashFirstFrame)
			{
				Result.EarlyNonZeroCount += CountNonZeroTexels(Layers);
			}

			if (bHasPreviousFrame && !bStepped)
			{
				Result.SkippedOutputChangedCount += Crc != PreviousCrc ? 1 : 0;
				Result.SkippedWindowChangedCount += Window != PreviousWindow ? 1 : 0;
			}

			if (bHasPreviousFrame && bStepped && Window != PreviousWindow)
			{
				const bool bIsTeleport = Frame == TeleportFrame;
				Result.WindowMoveCount += bIsTeleport ? 0 : 1;
				Result.bTeleportWindowJumped |= bIsTeleport && FMath::Max(FMath::Abs(Window.X - PreviousWindow.X), FMath::Abs(Window.Y - PreviousWindow.Y)) >= LayerResolution;
				Result.WindowMoveAfterHitCount += (bFirstStrikeWindowSet && !bIsTeleport && Frame > SmashLastFrame && Frame <= HitCheckLastFrame) ? 1 : 0;
			}

			if (Frame == SmashLastFrame)
			{
				HitWorldCell = FTDFog3DLayer::ComputeWorldCell(FVector2D(SmashTip.X, SmashTip.Y), Settings.CellSizeCm);
				WindowAtStrike = Window;
				bFirstStrikeWindowSet = true;
				Result.bHitWasRecorded = true;
				CheckFarCellsAreZero(Layers, HitWorldCell - Window, Result);
			}

			if (bFirstStrikeWindowSet && Frame >= SmashLastFrame && Frame <= HitCheckLastFrame)
			{
				const FIntPoint HitLocalCell = HitWorldCell - Window;
				const bool bInsideWindow = HitLocalCell.X >= 0 && HitLocalCell.Y >= 0 && HitLocalCell.X < LayerResolution && HitLocalCell.Y < LayerResolution;
				const double HitFill = bInsideWindow ? ReadChannel(Layers, HitLocalCell.X, HitLocalCell.Y, 0, 3) : 0.0;
				++Result.HitCheckCount;
				Result.HitCheckFailureCount += (bInsideWindow && HitFill < HitFillLimit) ? 0 : 1;
				Result.HitCheckAfterMoveCount += Window != WindowAtStrike ? 1 : 0;
				Result.HitFillMaximum = FMath::Max(Result.HitFillMaximum, HitFill);
				Result.HitFillTrace += FString::Printf(TEXT(" %d:%.3f%s"), Frame, HitFill, Window != WindowAtStrike ? TEXT("m") : TEXT(""));
				Result.HitFillAtStrike = Frame == SmashLastFrame ? HitFill : Result.HitFillAtStrike;
			}

			if (Frame == TeleportFrame)
			{
				CheckTeleportCells(Layers, Window, HandWorld, Settings, Result);
			}

			PreviousCrc = Crc;
			PreviousWindow = Window;
			bHasPreviousFrame = true;
		}

		Result.SeenRejectedStepCount = Layer.GetSeenRejectedStepCount();
		Result.SubmittedStepCount = Layer.GetSubmittedStepCount();
		Layer.Release();
		FlushRenderingCommands();
		return true;
	}

	void ReportRunResult(FAutomationTestBase& Test, const FString& RunLabel, const FRunResult& Result)
	{
		Test.AddInfo(FString::Printf(
			TEXT("Fog3D LayerOutputFrames %s: frames read %d (stepped %d, skipped %d), submitted %u, window moves %d (after hit %d), teleport jump %d, output vs state mismatches %d (max error %.3e), state window mismatches %d, hit fill at strike %.4f (max over %d checks %.4f, after move %d), far cells %d non-zero %d, teleport outside cells %d non-zero %d, inside non-zero %d, early non-zero %d, rejected seen %d slot %d, hit trace%s"),
			*RunLabel, Result.FrameCrcs.Num(), Result.SteppedFrameCount, Result.SkippedFrameCount, Result.SubmittedStepCount, Result.WindowMoveCount, Result.WindowMoveAfterHitCount,
			Result.bTeleportWindowJumped ? 1 : 0, Result.OutputStateMismatchCount, Result.OutputStateMaximumError, Result.StateWindowMismatchCount,
			Result.HitFillAtStrike, Result.HitCheckCount, Result.HitFillMaximum, Result.HitCheckAfterMoveCount, Result.FarCheckedCellCount, Result.FarNonZeroCount,
			Result.TeleportCheckedCellCount, Result.TeleportOutsideNonZeroCount, Result.TeleportInsideNonZeroCount, Result.EarlyNonZeroCount, Result.SeenRejectedStepCount, Result.SlotRejectedStepCount, *Result.HitFillTrace));
	}

	void VerifyRunResult(FAutomationTestBase& Test, const FString& RunLabel, const FRunResult& Result)
	{
		const auto Label = [&RunLabel](const TCHAR* Text)
		{
			return FString::Printf(TEXT("%s: %s"), *RunLabel, Text);
		};
		Test.TestEqual(*Label(TEXT("every frame was read back")), Result.FrameCrcs.Num(), TotalFrameCount);
		Test.TestEqual(*Label(TEXT("output reads that failed")), Result.ReadFailureCount, 0);
		Test.TestEqual(*Label(TEXT("state dumps that failed")), Result.DumpFailureCount, 0);
		Test.TestEqual(*Label(TEXT("all 120 first frames computed")), Result.PhaseOneUnsteppedFrameCount, 0);
		Test.TestEqual(*Label(TEXT("computed frames in total")), Result.SteppedFrameCount, PhaseOneFrameCount + (TotalFrameCount - PhaseOneFrameCount) / 2);
		Test.TestEqual(*Label(TEXT("skipped frames in the fast display stretch")), Result.SkippedFrameCount, (TotalFrameCount - PhaseOneFrameCount) / 2);
		Test.TestEqual(*Label(TEXT("computed frames that were submitted")), static_cast<int32>(Result.SubmittedStepCount), Result.SteppedFrameCount);
		Test.TestEqual(*Label(TEXT("computed frames the layer refused")), Result.SubmitRefusedCount, 0);
		Test.TestEqual(*Label(TEXT("skipped frames the layer accepted")), Result.SkippedSubmitAcceptedCount, 0);
		Test.TestEqual(*Label(TEXT("module rejections seen by the layer")), Result.SeenRejectedStepCount, 0);
		Test.TestEqual(*Label(TEXT("module rejections counted in the render slot")), Result.SlotRejectedStepCount, 0);
		Test.TestEqual(*Label(TEXT("output texels that are not finite")), Result.NonFiniteCount, 0);
		Test.TestEqual(*Label(TEXT("fill values outside -1..0")), Result.FillOutOfRangeCount, 0);
		Test.TestEqual(*Label(TEXT("displacement values beyond 500 cm")), Result.DisplacementOutOfRangeCount, 0);
		Test.TestEqual(*Label(TEXT("output values that differ from the same step state")), Result.OutputStateMismatchCount, 0);
		Test.TestEqual(*Label(TEXT("frames whose state window differs from the submitted window")), Result.StateWindowMismatchCount, 0);
		Test.TestEqual(*Label(TEXT("skipped frames whose output changed")), Result.SkippedOutputChangedCount, 0);
		Test.TestEqual(*Label(TEXT("skipped frames whose window changed")), Result.SkippedWindowChangedCount, 0);
		Test.TestEqual(*Label(TEXT("non-zero output texels before the first input")), Result.EarlyNonZeroCount, 0);
		Test.TestTrue(*Label(TEXT("the window followed the walking player at least twice")), Result.WindowMoveCount >= 2);
		Test.TestTrue(*Label(TEXT("the window moved after the strike")), Result.WindowMoveAfterHitCount >= 1);
		Test.TestTrue(*Label(TEXT("the strike spot was recorded")), Result.bHitWasRecorded);
		Test.TestTrue(*Label(TEXT("the strike spot was checked in a moved window")), Result.HitCheckAfterMoveCount >= 1);
		Test.TestEqual(*Label(TEXT("strike spot checks that failed")), Result.HitCheckFailureCount, 0);
		Test.TestTrue(*Label(TEXT("cells far from the strike were checked")), Result.FarCheckedCellCount > 0);
		Test.TestEqual(*Label(TEXT("non-zero cells farther than 30 cells from the strike")), Result.FarNonZeroCount, 0);
		Test.TestTrue(*Label(TEXT("the window jumped at the teleport")), Result.bTeleportWindowJumped);
		Test.TestTrue(*Label(TEXT("cells outside the hand reach were checked")), Result.TeleportCheckedCellCount > 0);
		Test.TestEqual(*Label(TEXT("non-zero cells outside the hand radius plus two cells after the teleport")), Result.TeleportOutsideNonZeroCount, 0);
		Test.TestTrue(*Label(TEXT("the hand sphere changed some cell after the teleport")), Result.TeleportInsideNonZeroCount > 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerOutputFramesTest,
	"TDGame.FluidFXRhi.Fog3D.LayerOutputFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerOutputFramesTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D LayerOutputFrames needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	FRunResult FirstRun;
	if (!RunLayerScenario(*this, TEXT("first run"), FirstRun))
	{
		return false;
	}
	ReportRunResult(*this, TEXT("first run"), FirstRun);
	VerifyRunResult(*this, TEXT("first run"), FirstRun);

	FRunResult SecondRun;
	if (!RunLayerScenario(*this, TEXT("second run"), SecondRun))
	{
		return false;
	}
	ReportRunResult(*this, TEXT("second run"), SecondRun);
	VerifyRunResult(*this, TEXT("second run"), SecondRun);

	int32 DifferentFrameCount = 0;
	int32 FirstDifferentFrame = -1;
	const int32 ComparedFrameCount = FMath::Min(FirstRun.FrameCrcs.Num(), SecondRun.FrameCrcs.Num());
	for (int32 Frame = 0; Frame < ComparedFrameCount; ++Frame)
	{
		const bool bDiffers = FirstRun.FrameCrcs[Frame] != SecondRun.FrameCrcs[Frame];
		DifferentFrameCount += bDiffers ? 1 : 0;
		FirstDifferentFrame = (bDiffers && FirstDifferentFrame < 0) ? Frame : FirstDifferentFrame;
	}
	TestEqual(TEXT("Both runs read the same number of frames"), FirstRun.FrameCrcs.Num(), SecondRun.FrameCrcs.Num());
	TestEqual(*FString::Printf(TEXT("Frames whose bytes differ between a new layer and a new target (first %d)"), FirstDifferentFrame), DifferentFrameCount, 0);

	VerifyMalformedOutputTargetRefused(*this, TEXT("target without unordered access"), false, PF_FloatRGBA);
	VerifyMalformedOutputTargetRefused(*this, TEXT("target with an eight bit format"), true, PF_B8G8R8A8);
	return true;
}

#endif
