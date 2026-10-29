#include "FluidFX/TDFluidMeshFieldEditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr int32 MeshFieldMinimumResolution = 16;
	constexpr int32 MeshFieldMaximumResolution = 1024;
	constexpr float MeshFieldSliceOffsetCm = 1.0e-3f;
	constexpr double MeshFieldUnreachedSquaredCells = 1.0e20;

	struct FMeshFieldSegment
	{
		FVector2f Start;
		FVector2f End;
	};

	bool MeshFieldIntersectTriangle(const FVector3f (&Corners)[3], float HeightCm, FMeshFieldSegment& OutSegment)
	{
		FVector2f Points[2];
		int32 PointCount = 0;
		for (int32 EdgeIndex = 0; EdgeIndex < 3 && PointCount < 2; ++EdgeIndex)
		{
			const FVector3f& From = Corners[EdgeIndex];
			const FVector3f& To = Corners[(EdgeIndex + 1) % 3];
			if ((From.Z > HeightCm) == (To.Z > HeightCm))
			{
				continue;
			}

			const float Alpha = (HeightCm - From.Z) / (To.Z - From.Z);
			Points[PointCount++] = FVector2f(FMath::Lerp(From.X, To.X, Alpha), FMath::Lerp(From.Y, To.Y, Alpha));
		}

		if (PointCount < 2)
		{
			return false;
		}

		OutSegment = { Points[0], Points[1] };
		return true;
	}

	void MeshFieldTransformLine(TArray<double>& Values, int32 First, int32 Count, int32 Stride, TArray<double>& Line, TArray<int32>& Parabolas, TArray<double>& Bounds)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Line[Index] = Values[First + Index * Stride];
		}

		int32 Rightmost = 0;
		Parabolas[0] = 0;
		Bounds[0] = -MeshFieldUnreachedSquaredCells;
		Bounds[1] = MeshFieldUnreachedSquaredCells;
		const auto IntersectParabolas = [&Line](int32 Query, int32 Vertex)
		{
			return ((Line[Query] + double(Query) * Query) - (Line[Vertex] + double(Vertex) * Vertex)) / (2.0 * Query - 2.0 * Vertex);
		};
		for (int32 Query = 1; Query < Count; ++Query)
		{
			double Intersection = IntersectParabolas(Query, Parabolas[Rightmost]);
			while (Rightmost > 0 && Intersection <= Bounds[Rightmost])
			{
				--Rightmost;
				Intersection = IntersectParabolas(Query, Parabolas[Rightmost]);
			}

			++Rightmost;
			Parabolas[Rightmost] = Query;
			Bounds[Rightmost] = Intersection;
			Bounds[Rightmost + 1] = MeshFieldUnreachedSquaredCells;
		}

		int32 Active = 0;
		for (int32 Query = 0; Query < Count; ++Query)
		{
			while (Bounds[Active + 1] < Query)
			{
				++Active;
			}
			const int32 Vertex = Parabolas[Active];
			Values[First + Query * Stride] = FMath::Min(double(Query - Vertex) * (Query - Vertex) + Line[Vertex], MeshFieldUnreachedSquaredCells);
		}
	}

	TArray<double> MeshFieldSquaredDistanceTo(const TArray<bool>& bIsInside, bool bTargetInside, int32 Resolution)
	{
		TArray<double> Values;
		Values.SetNumUninitialized(bIsInside.Num());
		for (int32 Index = 0; Index < bIsInside.Num(); ++Index)
		{
			Values[Index] = bIsInside[Index] == bTargetInside ? 0.0 : MeshFieldUnreachedSquaredCells;
		}

		TArray<double> Line;
		TArray<int32> Parabolas;
		TArray<double> Bounds;
		Line.SetNumZeroed(Resolution);
		Parabolas.SetNumZeroed(Resolution);
		Bounds.SetNumZeroed(Resolution + 1);
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			MeshFieldTransformLine(Values, Column, Resolution, Resolution, Line, Parabolas, Bounds);
		}
		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			MeshFieldTransformLine(Values, Row * Resolution, Resolution, 1, Line, Parabolas, Bounds);
		}
		return Values;
	}

	TArray<bool> MeshFieldClassifyCells(TConstArrayView<FMeshFieldSegment> Segments, const FBox2D& BoundsCm, int32 Resolution, double CellCm)
	{
		TArray<bool> bIsInside;
		bIsInside.Init(false, Resolution * Resolution);
		TArray<double> Crossings;
		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			const double CenterY = BoundsCm.Min.Y + (Row + 0.5) * CellCm;
			Crossings.Reset();
			for (const FMeshFieldSegment& Segment : Segments)
			{
				if ((Segment.Start.Y > CenterY) == (Segment.End.Y > CenterY))
				{
					continue;
				}
				const double Alpha = (CenterY - Segment.Start.Y) / (double(Segment.End.Y) - Segment.Start.Y);
				Crossings.Add(Segment.Start.X + Alpha * (double(Segment.End.X) - Segment.Start.X));
			}
			Crossings.Sort();

			int32 CrossingsLeft = 0;
			for (int32 Column = 0; Column < Resolution; ++Column)
			{
				const double CenterX = BoundsCm.Min.X + (Column + 0.5) * CellCm;
				while (CrossingsLeft < Crossings.Num() && Crossings[CrossingsLeft] <= CenterX)
				{
					++CrossingsLeft;
				}
				bIsInside[Row * Resolution + Column] = ((Crossings.Num() - CrossingsLeft) % 2) == 1;
			}
		}
		return bIsInside;
	}

	FString MeshFieldSerialize(const TSharedRef<FJsonObject>& Result)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Result, Writer);
		return Output;
	}

	FString MeshFieldFail(const FString& Message)
	{
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("error"));
		Result->SetStringField(TEXT("message"), Message);
		return MeshFieldSerialize(Result);
	}

	bool MeshFieldReadMesh(const UStaticMesh& Mesh, TArray<FVector3f>& OutPositions, TArray<int32>& OutTriangleIndices)
	{
		const FMeshDescription* MeshDescription = Mesh.GetMeshDescription(0);
		if (!MeshDescription)
		{
			return false;
		}

		FStaticMeshConstAttributes Attributes(*MeshDescription);
		const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
		TArray<int32> CompactIndices;
		CompactIndices.Init(INDEX_NONE, MeshDescription->Vertices().GetArraySize());
		for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
		{
			for (const FVertexID VertexID : MeshDescription->GetTriangleVertices(TriangleID))
			{
				int32& CompactIndex = CompactIndices[VertexID.GetValue()];
				if (CompactIndex == INDEX_NONE)
				{
					CompactIndex = OutPositions.Add(VertexPositions[VertexID]);
				}
				OutTriangleIndices.Add(CompactIndex);
			}
		}
		return !OutTriangleIndices.IsEmpty();
	}
}

FBox2D UTDFluidMeshFieldEditorLibrary::ResolveSliceBounds(TConstArrayView<FVector3f> Positions, float PaddingCm)
{
	FBox2D MeshBounds(ForceInit);
	for (const FVector3f& Position : Positions)
	{
		MeshBounds += FVector2D(Position.X, Position.Y);
	}

	if (!MeshBounds.bIsValid)
	{
		return MeshBounds;
	}

	const FVector2D Center = MeshBounds.GetCenter();
	const FVector2D HalfSize = MeshBounds.GetExtent();
	const double SquareHalfSize = FMath::Max(HalfSize.X, HalfSize.Y) + FMath::Max(PaddingCm, 0.0f);
	return FBox2D(Center - FVector2D(SquareHalfSize), Center + FVector2D(SquareHalfSize));
}

bool UTDFluidMeshFieldEditorLibrary::ComputeSliceSignedDistance(TConstArrayView<FVector3f> Positions, TConstArrayView<int32> TriangleIndices, float SliceHeightCm, const FBox2D& BoundsCm, int32 Resolution, TArray<float>& OutDistanceCm, TArray<FVector2f>& OutOutwardNormal)
{
	OutDistanceCm.Reset();
	OutOutwardNormal.Reset();
	const double CellCm = BoundsCm.bIsValid ? BoundsCm.GetSize().X / FMath::Max(Resolution, 1) : 0.0;
	if (Resolution < MeshFieldMinimumResolution || Resolution > MeshFieldMaximumResolution || CellCm <= 0.0 || TriangleIndices.Num() % 3 != 0)
	{
		return false;
	}

	const float HeightCm = SliceHeightCm + MeshFieldSliceOffsetCm;
	TArray<FMeshFieldSegment> Segments;
	for (int32 Index = 0; Index + 2 < TriangleIndices.Num(); Index += 3)
	{
		if (!Positions.IsValidIndex(TriangleIndices[Index]) || !Positions.IsValidIndex(TriangleIndices[Index + 1]) || !Positions.IsValidIndex(TriangleIndices[Index + 2]))
		{
			return false;
		}

		const FVector3f Corners[3] = { Positions[TriangleIndices[Index]], Positions[TriangleIndices[Index + 1]], Positions[TriangleIndices[Index + 2]] };
		FMeshFieldSegment Segment;
		if (MeshFieldIntersectTriangle(Corners, HeightCm, Segment))
		{
			Segments.Add(Segment);
		}
	}

	const TArray<bool> bIsInside = MeshFieldClassifyCells(Segments, BoundsCm, Resolution, CellCm);
	if (!bIsInside.Contains(true) || !bIsInside.Contains(false))
	{
		return false;
	}

	const TArray<double> SquaredCellsToInside = MeshFieldSquaredDistanceTo(bIsInside, true, Resolution);
	const TArray<double> SquaredCellsToOutside = MeshFieldSquaredDistanceTo(bIsInside, false, Resolution);
	OutDistanceCm.SetNumUninitialized(bIsInside.Num());
	for (int32 Index = 0; Index < bIsInside.Num(); ++Index)
	{
		const double Cells = bIsInside[Index] ? -(FMath::Sqrt(SquaredCellsToOutside[Index]) - 0.5) : FMath::Sqrt(SquaredCellsToInside[Index]) - 0.5;
		OutDistanceCm[Index] = static_cast<float>(Cells * CellCm);
	}

	OutOutwardNormal.SetNumUninitialized(bIsInside.Num());
	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const int32 Left = Row * Resolution + FMath::Max(Column - 1, 0);
			const int32 Right = Row * Resolution + FMath::Min(Column + 1, Resolution - 1);
			const int32 Down = FMath::Max(Row - 1, 0) * Resolution + Column;
			const int32 Up = FMath::Min(Row + 1, Resolution - 1) * Resolution + Column;
			const FVector2f Gradient(OutDistanceCm[Right] - OutDistanceCm[Left], OutDistanceCm[Up] - OutDistanceCm[Down]);
			OutOutwardNormal[Row * Resolution + Column] = Gradient.GetSafeNormal();
		}
	}
	return true;
}

void UTDFluidMeshFieldEditorLibrary::PackSliceFieldPixels(TConstArrayView<float> DistanceCm, TConstArrayView<FVector2f> OutwardNormal, TArray<FFloat16Color>& OutPixels)
{
	OutPixels.Reset();
	if (DistanceCm.Num() != OutwardNormal.Num())
	{
		return;
	}

	OutPixels.Reserve(DistanceCm.Num());
	for (int32 Index = 0; Index < DistanceCm.Num(); ++Index)
	{
		OutPixels.Add(FFloat16Color(FLinearColor(DistanceCm[Index], OutwardNormal[Index].X, OutwardNormal[Index].Y, 1.0f)));
	}
}

FString UTDFluidMeshFieldEditorLibrary::BakeMeshSliceField(UStaticMesh* Mesh, float SliceHeightLocalCm, int32 Resolution, float PaddingCm, const FString& TextureAssetPath)
{
	if (!Mesh)
	{
		return MeshFieldFail(TEXT("The mesh is null."));
	}

	FString PackageName = TextureAssetPath;
	int32 ObjectSeparatorIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), ObjectSeparatorIndex))
	{
		PackageName.LeftInline(ObjectSeparatorIndex);
	}
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		return MeshFieldFail(FString::Printf(TEXT("The texture path %s is not a valid long package name."), *TextureAssetPath));
	}

	TArray<FVector3f> Positions;
	TArray<int32> TriangleIndices;
	if (!MeshFieldReadMesh(*Mesh, Positions, TriangleIndices))
	{
		return MeshFieldFail(FString::Printf(TEXT("%s has no LOD 0 mesh description with triangles."), *Mesh->GetPathName()));
	}

	const FBox2D BoundsCm = ResolveSliceBounds(Positions, PaddingCm);
	TArray<float> DistanceCm;
	TArray<FVector2f> OutwardNormal;
	if (!ComputeSliceSignedDistance(Positions, TriangleIndices, SliceHeightLocalCm, BoundsCm, Resolution, DistanceCm, OutwardNormal))
	{
		return MeshFieldFail(FString::Printf(TEXT("The slice at %.3f cm of %s has no closed interior at resolution %d (allowed %d to %d)."), SliceHeightLocalCm, *Mesh->GetPathName(), Resolution, MeshFieldMinimumResolution, MeshFieldMaximumResolution));
	}

	TArray<FFloat16Color> Pixels;
	PackSliceFieldPixels(DistanceCm, OutwardNormal, Pixels);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return MeshFieldFail(FString::Printf(TEXT("The package %s is unavailable."), *PackageName));
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	if (UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), Package, *AssetName))
	{
		ExistingObject->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		ExistingObject->ClearFlags(RF_Public | RF_Standalone);
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	Texture->Source.Init(Resolution, Resolution, 1, 1, TSF_RGBA16F, reinterpret_cast<const uint8*>(Pixels.GetData()));
	Texture->CompressionSettings = TC_HDR;
	Texture->SRGB = false;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	Texture->PostEditChange();
	Texture->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Texture);

	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true) || !UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
	{
		return MeshFieldFail(FString::Printf(TEXT("Could not save %s."), *Filename));
	}

	float MinimumDistanceCm = TNumericLimits<float>::Max();
	float MaximumDistanceCm = TNumericLimits<float>::Lowest();
	int32 InsideCellCount = 0;
	for (const float Distance : DistanceCm)
	{
		MinimumDistanceCm = FMath::Min(MinimumDistanceCm, Distance);
		MaximumDistanceCm = FMath::Max(MaximumDistanceCm, Distance);
		InsideCellCount += Distance < 0.0f ? 1 : 0;
	}

	const FVector2D FieldSizeCm = BoundsCm.GetSize();
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("message"), TEXT(""));
	Result->SetStringField(TEXT("texture"), Texture->GetPathName());
	TArray<TSharedPtr<FJsonValue>> FieldSizeValues;
	FieldSizeValues.Add(MakeShared<FJsonValueNumber>(FieldSizeCm.X));
	FieldSizeValues.Add(MakeShared<FJsonValueNumber>(FieldSizeCm.Y));
	Result->SetArrayField(TEXT("field_size_cm"), FieldSizeValues);
	Result->SetNumberField(TEXT("min_distance_cm"), MinimumDistanceCm);
	Result->SetNumberField(TEXT("max_distance_cm"), MaximumDistanceCm);
	Result->SetNumberField(TEXT("inside_cells"), InsideCellCount);
	return MeshFieldSerialize(Result);
}
