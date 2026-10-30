#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidNearRippleLayer.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace TDFluidNearRippleRainTestPrivate
{
	constexpr float NearRainCellSizeCm = 600.0f / 256.0f;
	constexpr float NearRainSubstepSeconds = 1.0f / 120.0f;
	constexpr int32 NearRainBlockColumns = 100;
	constexpr int32 NearRainBlockRows = 2;
	constexpr int32 NearRainStepCount = 100;
	constexpr double NearRainSigmaLimit = 4.0;
	constexpr int32 NearRainWindowCells = 48;
	constexpr int32 NearRainShiftCells = 13;

	FTDFluidNearRippleRainInput MakeNearRainInput(float DropsPerSquareMeterPerSec, float DropRadiusCm)
	{
		FTDFluidNearRippleRainInput Input;
		Input.DropsPerSquareMeterPerSec = DropsPerSquareMeterPerSec;
		Input.DropImpulseCm = 0.15f;
		Input.DropRadiusCm = DropRadiusCm;
		Input.RandomSeed = 11u;
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRainReferenceTest,
	"TDGame.FluidFX.Rain.NearRainReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRainReferenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRainTestPrivate;

	const FTDFluidNearRippleRainFrame SpecFrame = FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(60.0f, 2.0f), NearRainCellSizeCm, NearRainSubstepSeconds);
	const double SpecBlockCm = 2.0 * NearRainCellSizeCm;
	const double SpecProbability = 60.0 * SpecBlockCm * SpecBlockCm * 1.0e-4 * NearRainSubstepSeconds;
	TestEqual(TEXT("Small drops use the minimum two cell block"), SpecFrame.BlockSizeCells, 2);
	TestTrue(TEXT("Drop probability follows rate times block area times substep"), FMath::Abs(SpecFrame.DropProbability - SpecProbability) <= 1.0e-6 * SpecProbability);
	TestTrue(TEXT("Sixty drops per square metre at 1/120 s is about 1.1e-3 per block"), SpecFrame.DropProbability > 1.0e-3f && SpecFrame.DropProbability < 1.2e-3f);
	TestEqual(TEXT("Radius converts to cells"), SpecFrame.DropRadiusCells, 2.0f / NearRainCellSizeCm, 1.0e-6f);

	const FTDFluidNearRippleRainFrame WideFrame = FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(60.0f, 7.5f), NearRainCellSizeCm, NearRainSubstepSeconds);
	TestEqual(TEXT("Wide drops round the block up to the radius"), WideFrame.BlockSizeCells, FMath::CeilToInt32(7.5f / NearRainCellSizeCm));
	TestEqual(TEXT("Dense rain clamps the block probability to one half"), FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(1.0e9f, 4.0f), NearRainCellSizeCm, NearRainSubstepSeconds).DropProbability, 0.5f);

	FTDFluidNearRippleRainInput NoImpulseInput = MakeNearRainInput(60.0f, 2.0f);
	NoImpulseInput.DropImpulseCm = 0.0f;
	const FTDFluidNearRippleRainFrame OffFrames[] = {
		FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(0.0f, 2.0f), NearRainCellSizeCm, NearRainSubstepSeconds),
		FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(std::numeric_limits<float>::quiet_NaN(), 2.0f), NearRainCellSizeCm, NearRainSubstepSeconds),
		FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(-5.0f, 2.0f), NearRainCellSizeCm, NearRainSubstepSeconds),
		FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(60.0f, 0.0f), NearRainCellSizeCm, NearRainSubstepSeconds),
		FTDFluidNearRippleLayer::MakeRainFrame(NoImpulseInput, NearRainCellSizeCm, NearRainSubstepSeconds),
		FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(60.0f, 2.0f), NearRainCellSizeCm, 0.0f)};
	for (const FTDFluidNearRippleRainFrame& OffFrame : OffFrames)
	{
		TestEqual(TEXT("Rain off gives zero probability"), OffFrame.DropProbability, 0.0f);
		float OffWeightSum = 0.0f;
		for (int32 CellIndex = -20; CellIndex < 20; ++CellIndex)
		{
			OffWeightSum += FTDFluidNearRippleLayer::ComputeRainDropWeight(FIntPoint(CellIndex, 3 - CellIndex), 5u, OffFrame);
		}
		TestEqual(TEXT("Rain off gives zero weight"), OffWeightSum, 0.0f);
	}

	for (const int32 BlockSizeCells : {2, 3, 5})
	{
		for (int32 WorldCell = -4 * BlockSizeCells - 1; WorldCell < 4 * BlockSizeCells + 1; ++WorldCell)
		{
			const int32 ExpectedBlock = FMath::FloorToInt32(static_cast<double>(WorldCell) / BlockSizeCells);
			const FIntPoint Block = FTDFluidNearRippleLayer::ComputeRainBlock(FIntPoint(WorldCell, -WorldCell), BlockSizeCells);
			TestEqual(*FString::Printf(TEXT("Block X of cell %d with block %d uses floor division"), WorldCell, BlockSizeCells), Block.X, ExpectedBlock);
			TestEqual(*FString::Printf(TEXT("Block Y of cell %d with block %d uses floor division"), -WorldCell, BlockSizeCells), Block.Y, FMath::FloorToInt32(static_cast<double>(-WorldCell) / BlockSizeCells));
		}
	}

	const FTDFluidNearRippleRainFrame CountFrame = FTDFluidNearRippleLayer::MakeRainFrame(MakeNearRainInput(2700.0f, 4.0f), NearRainCellSizeCm, NearRainSubstepSeconds);
	TestTrue(TEXT("Count frame probability is a few percent"), CountFrame.DropProbability > 0.02f && CountFrame.DropProbability < 0.1f);
	int32 DropCount = 0;
	int32 OutOfBlockDropCount = 0;
	int32 SilentDropCellCount = 0;
	int32 SampleCount = 0;
	for (int32 StepIndex = 0; StepIndex < NearRainStepCount; ++StepIndex)
	{
		for (int32 BlockY = -NearRainBlockRows / 2; BlockY < NearRainBlockRows / 2; ++BlockY)
		{
			for (int32 BlockX = -NearRainBlockColumns / 2; BlockX < NearRainBlockColumns / 2; ++BlockX)
			{
				++SampleCount;
				const FIntPoint Block(BlockX, BlockY);
				FVector2f DropOffsetCells;
				if (!FTDFluidNearRippleLayer::FindRainDropInBlock(Block, static_cast<uint32>(StepIndex), CountFrame, DropOffsetCells))
				{
					continue;
				}

				++DropCount;
				const float BlockSizeCells = static_cast<float>(CountFrame.BlockSizeCells);
				OutOfBlockDropCount += DropOffsetCells.X < 0.0f || DropOffsetCells.Y < 0.0f || DropOffsetCells.X >= BlockSizeCells || DropOffsetCells.Y >= BlockSizeCells ? 1 : 0;
				const FIntPoint DropCell = Block * CountFrame.BlockSizeCells + FIntPoint(FMath::FloorToInt32(DropOffsetCells.X), FMath::FloorToInt32(DropOffsetCells.Y));
				SilentDropCellCount += FTDFluidNearRippleLayer::ComputeRainDropWeight(DropCell, static_cast<uint32>(StepIndex), CountFrame) > 0.0f ? 0 : 1;
			}
		}
	}
	const double ExpectedDropCount = static_cast<double>(CountFrame.DropProbability) * SampleCount;
	const double DropCountSigma = FMath::Sqrt(ExpectedDropCount * (1.0 - CountFrame.DropProbability));
	AddInfo(FString::Printf(TEXT("TDFluidNearRain: samples=%d p=%.6f drops=%d expected=%.1f sigma=%.1f"), SampleCount, CountFrame.DropProbability, DropCount, ExpectedDropCount, DropCountSigma));
	TestEqual(TEXT("Twenty thousand block steps are sampled"), SampleCount, 20000);
	TestTrue(TEXT("Drop count stays within four sigma of p N"), FMath::Abs(DropCount - ExpectedDropCount) <= NearRainSigmaLimit * DropCountSigma);
	TestEqual(TEXT("Every drop lies inside its block"), OutOfBlockDropCount, 0);
	TestEqual(TEXT("The cell under every drop gets weight"), SilentDropCellCount, 0);

	const FIntPoint FirstWindowMinCell(-NearRainWindowCells / 2, -NearRainWindowCells / 2);
	const FIntPoint SecondWindowMinCell = FirstWindowMinCell + FIntPoint(NearRainShiftCells, -NearRainShiftCells);
	TMap<FIntPoint, float> FirstWindowWeights;
	for (int32 LocalY = 0; LocalY < NearRainWindowCells; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < NearRainWindowCells; ++LocalX)
		{
			const FIntPoint WorldCell = FirstWindowMinCell + FIntPoint(LocalX, LocalY);
			FirstWindowWeights.Add(WorldCell, FTDFluidNearRippleLayer::ComputeRainDropWeight(WorldCell, 7u, CountFrame));
		}
	}
	int32 SharedCellCount = 0;
	int32 ShiftMismatchCount = 0;
	float SharedWeightSum = 0.0f;
	for (int32 LocalY = 0; LocalY < NearRainWindowCells; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < NearRainWindowCells; ++LocalX)
		{
			const FIntPoint WorldCell = SecondWindowMinCell + FIntPoint(LocalX, LocalY);
			const float* FirstWeight = FirstWindowWeights.Find(WorldCell);
			if (!FirstWeight)
			{
				continue;
			}

			++SharedCellCount;
			const float SecondWeight = FTDFluidNearRippleLayer::ComputeRainDropWeight(WorldCell, 7u, CountFrame);
			ShiftMismatchCount += FMemory::Memcmp(FirstWeight, &SecondWeight, sizeof(float)) == 0 ? 0 : 1;
			SharedWeightSum += SecondWeight;
		}
	}
	TestEqual(TEXT("Shifted windows share the expected world cells"), SharedCellCount, (NearRainWindowCells - NearRainShiftCells) * (NearRainWindowCells - NearRainShiftCells));
	TestEqual(TEXT("Shared world cells keep identical weights after a window shift"), ShiftMismatchCount, 0);
	TestTrue(TEXT("Shared region across the zero axis carries drops"), SharedWeightSum > 0.0f);
	return !HasAnyErrors();
}

#endif
