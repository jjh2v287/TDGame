#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidMeshFieldEditorLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

namespace TDFluidMeshFieldEditorTestPrivate
{
	constexpr float MeshFieldCubeHalfSizeCm = 50.0f;
	constexpr float MeshFieldPaddingCm = 50.0f;
	constexpr int32 MeshFieldResolution = 64;

	void BuildMeshFieldCube(TArray<FVector3f>& OutPositions, TArray<int32>& OutTriangleIndices)
	{
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			OutPositions.Add(FVector3f(
				(Corner & 1) ? MeshFieldCubeHalfSizeCm : -MeshFieldCubeHalfSizeCm,
				(Corner & 2) ? MeshFieldCubeHalfSizeCm : -MeshFieldCubeHalfSizeCm,
				(Corner & 4) ? MeshFieldCubeHalfSizeCm : -MeshFieldCubeHalfSizeCm));
		}

		const int32 Faces[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
		for (const int32 (&Face)[4] : Faces)
		{
			OutTriangleIndices.Append({ Face[0], Face[1], Face[2], Face[0], Face[2], Face[3] });
		}
	}

	bool ComputeMeshFieldCube(FBox2D& OutBounds, TArray<float>& OutDistanceCm, TArray<FVector2f>& OutNormal)
	{
		TArray<FVector3f> Positions;
		TArray<int32> TriangleIndices;
		BuildMeshFieldCube(Positions, TriangleIndices);
		OutBounds = UTDFluidMeshFieldEditorLibrary::ResolveSliceBounds(Positions, MeshFieldPaddingCm);
		return UTDFluidMeshFieldEditorLibrary::ComputeSliceSignedDistance(Positions, TriangleIndices, 0.0f, OutBounds, MeshFieldResolution, OutDistanceCm, OutNormal);
	}

	int32 FindMeshFieldCell(const FBox2D& Bounds, const FVector2D& PointCm)
	{
		const double CellCm = Bounds.GetSize().X / MeshFieldResolution;
		const int32 Column = FMath::Clamp(FMath::FloorToInt32((PointCm.X - Bounds.Min.X) / CellCm), 0, MeshFieldResolution - 1);
		const int32 Row = FMath::Clamp(FMath::FloorToInt32((PointCm.Y - Bounds.Min.Y) / CellCm), 0, MeshFieldResolution - 1);
		return Row * MeshFieldResolution + Column;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMeshFieldCubeSliceTest,
	"TDGameEditor.FluidFX.MeshField.CubeSliceSignedDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMeshFieldCubeSliceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidMeshFieldEditorTestPrivate;
	FBox2D Bounds;
	TArray<float> DistanceCm;
	TArray<FVector2f> Normal;
	if (!TestTrue(TEXT("The cube slice has a closed interior"), ComputeMeshFieldCube(Bounds, DistanceCm, Normal)))
	{
		return false;
	}

	const double CellCm = Bounds.GetSize().X / MeshFieldResolution;
	TestEqual(TEXT("The square field spans the cube plus padding"), Bounds.GetSize().X, 2.0 * (MeshFieldCubeHalfSizeCm + MeshFieldPaddingCm), 1.0e-3);
	TestEqual(TEXT("The distance grid has one value per cell"), DistanceCm.Num(), MeshFieldResolution * MeshFieldResolution);

	const float CenterDistance = DistanceCm[FindMeshFieldCell(Bounds, FVector2D::ZeroVector)];
	const float FaceDistance = DistanceCm[FindMeshFieldCell(Bounds, FVector2D(MeshFieldCubeHalfSizeCm, 0.0))];
	const float OutsideDistance = DistanceCm[FindMeshFieldCell(Bounds, FVector2D(MeshFieldCubeHalfSizeCm + 30.0, 0.0))];
	AddInfo(FString::Printf(TEXT("TDFluidMeshFieldCube: cell=%.4f center=%.3f face=%.3f outside30=%.3f"), CellCm, CenterDistance, FaceDistance, OutsideDistance));
	TestTrue(TEXT("The center is 50 cm inside within one cell"), FMath::Abs(CenterDistance + MeshFieldCubeHalfSizeCm) <= CellCm);
	TestTrue(TEXT("The face is at zero within one cell"), FMath::Abs(FaceDistance) <= CellCm);
	TestTrue(TEXT("A point 30 cm outside reads +30 within one cell"), FMath::Abs(OutsideDistance - 30.0f) <= CellCm);

	const double ProbeCm = MeshFieldCubeHalfSizeCm + 30.0;
	const TPair<FVector2D, FVector2f> NormalProbes[] = {
		{ FVector2D(ProbeCm, 0.0), FVector2f(1.0f, 0.0f) },
		{ FVector2D(-ProbeCm, 0.0), FVector2f(-1.0f, 0.0f) },
		{ FVector2D(0.0, ProbeCm), FVector2f(0.0f, 1.0f) },
		{ FVector2D(0.0, -ProbeCm), FVector2f(0.0f, -1.0f) },
	};
	for (const TPair<FVector2D, FVector2f>& Probe : NormalProbes)
	{
		const FVector2f CellNormal = Normal[FindMeshFieldCell(Bounds, Probe.Key)];
		TestTrue(*FString::Printf(TEXT("The outward normal at (%.0f, %.0f) points along (%.0f, %.0f)"), Probe.Key.X, Probe.Key.Y, Probe.Value.X, Probe.Value.Y), FVector2f::DotProduct(CellNormal, Probe.Value) >= 0.95f);
	}

	int32 NonFiniteCount = 0;
	for (int32 Index = 0; Index < DistanceCm.Num(); ++Index)
	{
		NonFiniteCount += FMath::IsFinite(DistanceCm[Index]) && FMath::IsFinite(Normal[Index].X) && FMath::IsFinite(Normal[Index].Y) ? 0 : 1;
	}
	TestEqual(TEXT("Every distance and normal is finite"), NonFiniteCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMeshFieldDeterminismTest,
	"TDGameEditor.FluidFX.MeshField.BakeIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMeshFieldDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidMeshFieldEditorTestPrivate;
	uint32 Hashes[2] = {};
	for (uint32& Hash : Hashes)
	{
		FBox2D Bounds;
		TArray<float> DistanceCm;
		TArray<FVector2f> Normal;
		if (!TestTrue(TEXT("The cube slice has a closed interior"), ComputeMeshFieldCube(Bounds, DistanceCm, Normal)))
		{
			return false;
		}

		TArray<FFloat16Color> Pixels;
		UTDFluidMeshFieldEditorLibrary::PackSliceFieldPixels(DistanceCm, Normal, Pixels);
		TestEqual(TEXT("The packed field has one half-float pixel per cell"), Pixels.Num(), MeshFieldResolution * MeshFieldResolution);
		Hash = FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
	}

	AddInfo(FString::Printf(TEXT("TDFluidMeshFieldHash: first=%08x second=%08x"), Hashes[0], Hashes[1]));
	TestTrue(TEXT("Two bakes of the same mesh produce the same bytes"), Hashes[0] == Hashes[1]);
	return true;
}

#endif
