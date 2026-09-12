#include "World/TDWorldGeneration.h"

namespace
{
	constexpr float TD_ROAD_LINK_CM = 2000.0f * 1.01f;
	constexpr float TD_TERRAIN_SAMPLE_RADIUS_CM = 800.0f;
	constexpr float TD_STEEP_FRACTION_LIMIT = 0.5f;

	const FName TD_CHECK_ENTRANCE_SPACING(TEXT("entrance_spacing"));
	const FName TD_CHECK_POI_DENSITY(TEXT("poi_density"));
	const FName TD_CHECK_ROAD_REACH(TEXT("road_reach"));
	const FName TD_CHECK_TERRAIN_FIT(TEXT("terrain_fit"));
	const FName TD_CHECK_STREAMING_SAFETY(TEXT("streaming_safety"));
	const FName TD_CHECK_PLAY_DENSITY(TEXT("play_density"));

	struct FTDContentTarget
	{
		FName Id;
		FVector LocationCm = FVector::ZeroVector;
		bool bApproachOnly = false;
		float ApproachYawDeg = 0.0f;
	};

	struct FTDRoadPointGraph
	{
		TArray<FVector> PointsCm;
		TArray<TArray<int32>> Neighbors;

		void Build(const FTDWorldLayout& Layout)
		{
			for (const FTDRoadPolyline& Road : Layout.Roads)
			{
				PointsCm.Append(Road.PointsCm);
			}
			Neighbors.SetNum(PointsCm.Num());
			const float LinkSquared = FMath::Square(TD_ROAD_LINK_CM);
			for (int32 A = 0; A < PointsCm.Num(); ++A)
			{
				for (int32 B = A + 1; B < PointsCm.Num(); ++B)
				{
					if (FVector::DistSquared2D(PointsCm[A], PointsCm[B]) > LinkSquared)
					{
						continue;
					}
					Neighbors[A].Add(B);
					Neighbors[B].Add(A);
				}
			}
		}

		void FloodFrom(const TArray<int32>& Seeds, const TArray<bool>* Allowed, TArray<bool>& OutVisited) const
		{
			OutVisited.Init(false, PointsCm.Num());
			TArray<int32> Queue = Seeds;
			for (int32 Seed : Seeds)
			{
				OutVisited[Seed] = true;
			}
			while (Queue.Num() > 0)
			{
				const int32 Current = Queue.Pop(EAllowShrinking::No);
				for (int32 Next : Neighbors[Current])
				{
					if (OutVisited[Next] || (Allowed != nullptr && !(*Allowed)[Next]))
					{
						continue;
					}
					OutVisited[Next] = true;
					Queue.Add(Next);
				}
			}
		}

		int32 FarthestPoint(int32 Source, const TArray<bool>& Allowed, float& OutDistance) const
		{
			TArray<float> Distance;
			Distance.Init(TNumericLimits<float>::Max(), PointsCm.Num());
			TArray<bool> Settled;
			Settled.Init(false, PointsCm.Num());
			Distance[Source] = 0.0f;
			int32 Farthest = Source;
			OutDistance = 0.0f;
			while (true)
			{
				int32 Current = INDEX_NONE;
				for (int32 Index = 0; Index < PointsCm.Num(); ++Index)
				{
					if (!Settled[Index] && Allowed[Index] && Distance[Index] < TNumericLimits<float>::Max() && (Current == INDEX_NONE || Distance[Index] < Distance[Current]))
					{
						Current = Index;
					}
				}
				if (Current == INDEX_NONE)
				{
					break;
				}
				Settled[Current] = true;
				if (Distance[Current] > OutDistance)
				{
					OutDistance = Distance[Current];
					Farthest = Current;
				}
				for (int32 Next : Neighbors[Current])
				{
					if (!Allowed[Next] || Settled[Next])
					{
						continue;
					}
					const float Candidate = Distance[Current] + FVector::Dist2D(PointsCm[Current], PointsCm[Next]);
					Distance[Next] = FMath::Min(Distance[Next], Candidate);
				}
			}
			return Farthest;
		}
	};

	void CollectContentTargets(const FTDWorldLayout& Layout, TArray<FTDContentTarget>& OutTargets)
	{
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			OutTargets.Add({ Poi.PoiId, Poi.LocationCm });
		}
		for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
		{
			OutTargets.Add({ Entrance.DungeonId, Entrance.LocationCm, true, Entrance.Yaw });
		}
	}

	const FTDWorldAnchor* FindStartAnchor(const FTDWorldLayout& Layout)
	{
		const FTDWorldAnchor* Town = Layout.FindAnchor(ETDWorldAnchorKind::Town);
		return Town != nullptr ? Town : Layout.FindAnchor(ETDWorldAnchorKind::PlayerStart);
	}

	float CheckEntranceSpacing(const FTDWorldLayout& Layout, const FTDWorldValidator::FSettings& Settings, FTDValidationReport& Report)
	{
		int32 ErrorCount = 0;
		float MinPairCm = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Layout.Entrances.Num(); ++A)
		{
			for (int32 B = A + 1; B < Layout.Entrances.Num(); ++B)
			{
				const float Distance = FVector::Dist2D(Layout.Entrances[A].LocationCm, Layout.Entrances[B].LocationCm);
				MinPairCm = FMath::Min(MinPairCm, Distance);
				if (Distance >= Settings.MinEntranceSpacingCm)
				{
					continue;
				}
				++ErrorCount;
				Report.Add(ETDValidationSeverity::Error, TD_CHECK_ENTRANCE_SPACING, FString::Printf(TEXT("%s ~ %s 거리 %.0f cm < 최소 %.0f cm"), *Layout.Entrances[A].DungeonId.ToString(), *Layout.Entrances[B].DungeonId.ToString(), Distance, Settings.MinEntranceSpacingCm), Layout.Entrances[A].LocationCm, Layout.Entrances[A].DungeonId);
			}
		}
		if (Layout.Entrances.Num() == 0)
		{
			++ErrorCount;
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_ENTRANCE_SPACING, TEXT("던전 입구가 없습니다"));
		}
		Report.Add(ETDValidationSeverity::Info, TD_CHECK_ENTRANCE_SPACING, FString::Printf(TEXT("입구 %d개, 최소 간격 %.0f cm (기준 ≥ %.0f cm)"), Layout.Entrances.Num(), MinPairCm < TNumericLimits<float>::Max() ? MinPairCm : 0.0f, Settings.MinEntranceSpacingCm));
		return ErrorCount == 0 ? 1.0f : 0.0f;
	}

	float CheckPoiDensity(const FTDWorldLayout& Layout, const FTDWorldValidator::FSettings& Settings, FTDValidationReport& Report)
	{
		TArray<const FTDPoiPlacement*> Pois;
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			if (Poi.Kind != ETDPoiKind::Arena && Poi.Kind != ETDPoiKind::EventArea)
			{
				Pois.Add(&Poi);
			}
		}
		int32 ErrorCount = 0;
		if (Pois.Num() < Settings.PoiCountRange.X || Pois.Num() > Settings.PoiCountRange.Y)
		{
			++ErrorCount;
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_POI_DENSITY, FString::Printf(TEXT("POI 개수 %d개가 범위 %d~%d 밖"), Pois.Num(), Settings.PoiCountRange.X, Settings.PoiCountRange.Y));
		}
		float MinPairCm = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Pois.Num(); ++A)
		{
			for (int32 B = A + 1; B < Pois.Num(); ++B)
			{
				const float Distance = FVector::Dist2D(Pois[A]->LocationCm, Pois[B]->LocationCm);
				MinPairCm = FMath::Min(MinPairCm, Distance);
				if (Distance >= Settings.MinPoiSpacingCm)
				{
					continue;
				}
				++ErrorCount;
				Report.Add(ETDValidationSeverity::Error, TD_CHECK_POI_DENSITY, FString::Printf(TEXT("%s ~ %s 거리 %.0f cm < 최소 %.0f cm"), *Pois[A]->PoiId.ToString(), *Pois[B]->PoiId.ToString(), Distance, Settings.MinPoiSpacingCm), Pois[A]->LocationCm, Pois[A]->PoiId);
			}
		}
		Report.Add(ETDValidationSeverity::Info, TD_CHECK_POI_DENSITY, FString::Printf(TEXT("POI %d개 (범위 %d~%d), 최소 간격 %.0f cm (기준 ≥ %.0f cm)"), Pois.Num(), Settings.PoiCountRange.X, Settings.PoiCountRange.Y, MinPairCm < TNumericLimits<float>::Max() ? MinPairCm : 0.0f, Settings.MinPoiSpacingCm));
		return ErrorCount == 0 ? 1.0f : 0.0f;
	}

	float DistanceToNearestPoint(const TArray<FVector>& PointsCm, const TArray<bool>* Mask, const FVector& LocationCm)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < PointsCm.Num(); ++Index)
		{
			if (Mask != nullptr && !(*Mask)[Index])
			{
				continue;
			}
			Best = FMath::Min(Best, FVector::Dist2D(PointsCm[Index], LocationCm));
		}
		return Best;
	}

	float CheckRoadReach(const FTDWorldLayout& Layout, const FTDRoadPointGraph& Graph, const FTDWorldValidator::FSettings& Settings, FTDValidationReport& Report)
	{
		TArray<FTDContentTarget> Targets;
		CollectContentTargets(Layout, Targets);
		const FTDWorldAnchor* Start = FindStartAnchor(Layout);
		if (Start == nullptr)
		{
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_ROAD_REACH, TEXT("마을(Town) 또는 PlayerStart 앵커가 없습니다"));
			return 0.0f;
		}
		TArray<int32> Seeds;
		for (int32 Index = 0; Index < Graph.PointsCm.Num(); ++Index)
		{
			if (FVector::Dist2D(Graph.PointsCm[Index], Start->LocationCm) <= Settings.RoadReachCm)
			{
				Seeds.Add(Index);
			}
		}
		if (Seeds.Num() == 0)
		{
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_ROAD_REACH, FString::Printf(TEXT("%s %.0f cm 안에 도로 점이 없습니다"), *Start->AnchorId.ToString(), Settings.RoadReachCm), Start->LocationCm, Start->AnchorId);
			return 0.0f;
		}
		TArray<bool> Connected;
		Graph.FloodFrom(Seeds, nullptr, Connected);
		int32 Reached = 0;
		for (const FTDContentTarget& Target : Targets)
		{
			const float ConnectedDistance = DistanceToNearestPoint(Graph.PointsCm, &Connected, Target.LocationCm);
			if (ConnectedDistance <= Settings.RoadReachCm)
			{
				++Reached;
				continue;
			}
			const float AnyDistance = DistanceToNearestPoint(Graph.PointsCm, nullptr, Target.LocationCm);
			const FString Reason = AnyDistance <= Settings.RoadReachCm
				? FString::Printf(TEXT("도로가 %.0f cm 거리에 있으나 마을 도로망과 끊김 (연결 도로까지 %.0f cm)"), AnyDistance, ConnectedDistance)
				: FString::Printf(TEXT("가장 가까운 도로 %.0f cm > 허용 %.0f cm"), AnyDistance, Settings.RoadReachCm);
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_ROAD_REACH, FString::Printf(TEXT("%s: %s"), *Target.Id.ToString(), *Reason), Target.LocationCm, Target.Id);
		}
		Report.Add(ETDValidationSeverity::Info, TD_CHECK_ROAD_REACH, FString::Printf(TEXT("대상 %d개 중 %d개 도달 (도로 점 %d개, 허용 %.0f cm)"), Targets.Num(), Reached, Graph.PointsCm.Num(), Settings.RoadReachCm));
		return Targets.Num() > 0 ? static_cast<float>(Reached) / Targets.Num() : 0.0f;
	}

	float CheckTerrainFit(const FTDWorldLayout& Layout, const FTDTerrainSampler* Terrain, const FTDWorldValidator::FSettings& Settings, FTDValidationReport& Report)
	{
		if (Terrain == nullptr || !Terrain->IsValid())
		{
			Report.Add(ETDValidationSeverity::Info, TD_CHECK_TERRAIN_FIT, TEXT("지형 샘플러 없음: 평지로 간주하여 통과"));
			return 1.0f;
		}
		TArray<FTDContentTarget> Targets;
		const FTDWorldAnchor* PlayerStart = Layout.FindAnchor(ETDWorldAnchorKind::PlayerStart);
		const FTDWorldAnchor* Start = PlayerStart != nullptr ? PlayerStart : FindStartAnchor(Layout);
		if (Start != nullptr)
		{
			Targets.Add({ Start->AnchorId, Start->LocationCm });
		}
		CollectContentTargets(Layout, Targets);

		const FVector2D Offsets[4] = { FVector2D(TD_TERRAIN_SAMPLE_RADIUS_CM, 0.0f), FVector2D(-TD_TERRAIN_SAMPLE_RADIUS_CM, 0.0f), FVector2D(0.0f, TD_TERRAIN_SAMPLE_RADIUS_CM), FVector2D(0.0f, -TD_TERRAIN_SAMPLE_RADIUS_CM) };
		int32 Fit = 0;
		for (const FTDContentTarget& Target : Targets)
		{
			TArray<FVector2D, TInlineAllocator<5>> Samples;
			if (Target.bApproachOnly)
			{
				const FVector2D Forward(FMath::Cos(FMath::DegreesToRadians(Target.ApproachYawDeg)), FMath::Sin(FMath::DegreesToRadians(Target.ApproachYawDeg)));
				const FVector2D Left(FMath::Cos(FMath::DegreesToRadians(Target.ApproachYawDeg + 40.0f)), FMath::Sin(FMath::DegreesToRadians(Target.ApproachYawDeg + 40.0f)));
				const FVector2D Right(FMath::Cos(FMath::DegreesToRadians(Target.ApproachYawDeg - 40.0f)), FMath::Sin(FMath::DegreesToRadians(Target.ApproachYawDeg - 40.0f)));
				const FVector2D Base(Target.LocationCm);
				Samples.Add(Base + Forward * (TD_TERRAIN_SAMPLE_RADIUS_CM * 0.5f));
				Samples.Add(Base + Forward * TD_TERRAIN_SAMPLE_RADIUS_CM);
				Samples.Add(Base + Left * TD_TERRAIN_SAMPLE_RADIUS_CM);
				Samples.Add(Base + Right * TD_TERRAIN_SAMPLE_RADIUS_CM);
				Samples.Add(Base + Forward * (TD_TERRAIN_SAMPLE_RADIUS_CM * 1.5f));
			}
			else
			{
				Samples.Add(FVector2D(Target.LocationCm));
				for (const FVector2D& Offset : Offsets)
				{
					Samples.Add(FVector2D(Target.LocationCm) + Offset);
				}
			}
			const FVector2D Center = Samples[0];
			const float CenterSlope = Terrain->SampleSlope(Center);
			const float CenterHeight = Terrain->SampleHeight(Center);
			int32 SteepCount = 0;
			for (const FVector2D& Sample : Samples)
			{
				SteepCount += Terrain->SampleSlope(Sample) > Settings.MaxSlopeDeg ? 1 : 0;
			}
			const float SteepFraction = static_cast<float>(SteepCount) / Samples.Num();
			TArray<FString> Reasons;
			if (CenterHeight < Terrain->WaterLevelCm)
			{
				Reasons.Add(FString::Printf(TEXT("중심 높이 %.0f cm 가 수면 %.0f cm 아래"), CenterHeight, Terrain->WaterLevelCm));
			}
			if (CenterSlope > Settings.MaxSlopeDeg)
			{
				Reasons.Add(FString::Printf(TEXT("중심 경사 %.1f° > %.0f°"), CenterSlope, Settings.MaxSlopeDeg));
			}
			if (SteepFraction > TD_STEEP_FRACTION_LIMIT)
			{
				Reasons.Add(FString::Printf(TEXT("%s반경 %.0f cm 표본의 %.0f%% 가 경사 %.0f° 초과"), Target.bApproachOnly ? TEXT("접근로 ") : TEXT(""), TD_TERRAIN_SAMPLE_RADIUS_CM, SteepFraction * 100.0f, Settings.MaxSlopeDeg));
			}
			if (Reasons.Num() == 0)
			{
				++Fit;
				continue;
			}
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_TERRAIN_FIT, FString::Printf(TEXT("%s: %s"), *Target.Id.ToString(), *FString::Join(Reasons, TEXT("; "))), Target.LocationCm, Target.Id);
		}
		Report.Add(ETDValidationSeverity::Info, TD_CHECK_TERRAIN_FIT, FString::Printf(TEXT("대상 %d개 중 %d개 적합 (최대 경사 %.0f°, 수면 %.0f cm)"), Targets.Num(), Fit, Settings.MaxSlopeDeg, Terrain->WaterLevelCm));
		return Targets.Num() > 0 ? static_cast<float>(Fit) / Targets.Num() : 0.0f;
	}

	float DistanceToBox2D(const FBox2D& Box, const FVector2D& Point)
	{
		const FVector2D Clamped(FMath::Clamp(Point.X, Box.Min.X, Box.Max.X), FMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y));
		return FVector2D::Distance(Clamped, Point);
	}

	float CheckStreamingSafety(const FTDWorldLayout& Layout, const UTDDungeonAtlasDefinition* Atlas, const FTDWorldValidator::FSettings& Settings, FTDValidationReport& Report)
	{
		if (Atlas == nullptr)
		{
			Report.Add(ETDValidationSeverity::Info, TD_CHECK_STREAMING_SAFETY, TEXT("던전 아틀라스 없음: 통과"));
			return 1.0f;
		}
		int32 ErrorCount = 0;
		const float RequiredPitch = 2.0f * Atlas->LoadingRangeCm;
		for (int32 A = 0; A < Atlas->Slots.Num(); ++A)
		{
			const FVector OriginA = Atlas->GetSlotOriginCm(Atlas->Slots[A].SlotIndex);
			for (int32 B = A + 1; B < Atlas->Slots.Num(); ++B)
			{
				const FVector OriginB = Atlas->GetSlotOriginCm(Atlas->Slots[B].SlotIndex);
				const float Distance = FVector::Dist2D(OriginA, OriginB);
				if (Distance >= RequiredPitch)
				{
					continue;
				}
				++ErrorCount;
				Report.Add(ETDValidationSeverity::Error, TD_CHECK_STREAMING_SAFETY, FString::Printf(TEXT("슬롯 %s ~ %s 원점 거리 %.0f cm < 로딩 범위 합 %.0f cm"), *Atlas->Slots[A].DungeonId.ToString(), *Atlas->Slots[B].DungeonId.ToString(), Distance, RequiredPitch), OriginA, Atlas->Slots[A].DungeonId);
			}
			const float FieldDistance = Layout.BoundsCm.bIsValid ? DistanceToBox2D(Layout.BoundsCm, FVector2D(OriginA)) : TNumericLimits<float>::Max();
			if (FieldDistance >= Settings.FieldSlotClearanceCm)
			{
				continue;
			}
			++ErrorCount;
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_STREAMING_SAFETY, FString::Printf(TEXT("슬롯 %s 와 필드 경계 거리 %.0f cm < 필요 %.0f cm"), *Atlas->Slots[A].DungeonId.ToString(), FieldDistance, Settings.FieldSlotClearanceCm), OriginA, Atlas->Slots[A].DungeonId);
		}
		Report.Add(ETDValidationSeverity::Info, TD_CHECK_STREAMING_SAFETY, FString::Printf(TEXT("슬롯 %d개, 로딩 범위 %.0f cm (필요 간격 ≥ %.0f cm, 필드 여유 ≥ %.0f cm)"), Atlas->Slots.Num(), Atlas->LoadingRangeCm, RequiredPitch, Settings.FieldSlotClearanceCm));
		return ErrorCount == 0 ? 1.0f : 0.0f;
	}

	float CheckPlayDensity(const FTDWorldLayout& Layout, const FTDRoadPointGraph& Graph, const FTDWorldValidator::FSettings& Settings, FTDValidationReport& Report)
	{
		TArray<FTDContentTarget> Content;
		const FTDWorldAnchor* Start = FindStartAnchor(Layout);
		if (Start != nullptr)
		{
			Content.Add({ Start->AnchorId, Start->LocationCm });
		}
		CollectContentTargets(Layout, Content);
		if (Content.Num() == 0 || Graph.PointsCm.Num() == 0)
		{
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_PLAY_DENSITY, TEXT("콘텐츠 또는 도로 없음"));
			return 0.0f;
		}
		TArray<bool> Far;
		Far.Init(false, Graph.PointsCm.Num());
		for (int32 Index = 0; Index < Graph.PointsCm.Num(); ++Index)
		{
			float Nearest = TNumericLimits<float>::Max();
			for (const FTDContentTarget& Target : Content)
			{
				Nearest = FMath::Min(Nearest, FVector::Dist2D(Graph.PointsCm[Index], Target.LocationCm));
			}
			Far[Index] = Nearest > Settings.ContentFarCm;
		}

		TArray<bool> Assigned;
		Assigned.Init(false, Graph.PointsCm.Num());
		int32 ErrorCount = 0;
		int32 SegmentCount = 0;
		float LongestCm = 0.0f;
		for (int32 Index = 0; Index < Graph.PointsCm.Num(); ++Index)
		{
			if (!Far[Index] || Assigned[Index])
			{
				continue;
			}
			TArray<bool> Component;
			Graph.FloodFrom({ Index }, &Far, Component);
			for (int32 Member = 0; Member < Component.Num(); ++Member)
			{
				Assigned[Member] = Assigned[Member] || Component[Member];
			}
			float Unused = 0.0f;
			const int32 EndA = Graph.FarthestPoint(Index, Component, Unused);
			float LengthCm = 0.0f;
			const int32 EndB = Graph.FarthestPoint(EndA, Component, LengthCm);
			++SegmentCount;
			LongestCm = FMath::Max(LongestCm, LengthCm);
			if (LengthCm <= Settings.EmptyMaxCm)
			{
				continue;
			}
			++ErrorCount;
			const FVector Middle = (Graph.PointsCm[EndA] + Graph.PointsCm[EndB]) * 0.5f;
			Report.Add(ETDValidationSeverity::Error, TD_CHECK_PLAY_DENSITY, FString::Printf(TEXT("콘텐츠에서 %.0f cm 이상 떨어진 도로 %.0f cm 연속 > 허용 %.0f cm ((%.0f, %.0f) → (%.0f, %.0f))"), Settings.ContentFarCm, LengthCm, Settings.EmptyMaxCm, Graph.PointsCm[EndA].X, Graph.PointsCm[EndA].Y, Graph.PointsCm[EndB].X, Graph.PointsCm[EndB].Y), Middle);
		}
		Report.Add(ETDValidationSeverity::Info, TD_CHECK_PLAY_DENSITY, FString::Printf(TEXT("콘텐츠 %.0f cm 밖 도로 구간 %d개, 최장 %.0f cm (허용 ≤ %.0f cm)"), Settings.ContentFarCm, SegmentCount, LongestCm, Settings.EmptyMaxCm));
		return ErrorCount == 0 ? 1.0f : 0.0f;
	}
}

FTDValidationReport FTDWorldValidator::Validate(const FTDWorldLayout& Layout, const UTDDungeonAtlasDefinition* Atlas, const FTDTerrainSampler* Terrain, const FSettings& Settings)
{
	FTDValidationReport Report;
	FTDRoadPointGraph Graph;
	Graph.Build(Layout);

	float Score = 0.0f;
	Score += 15.0f * CheckEntranceSpacing(Layout, Settings, Report);
	Score += 15.0f * CheckPoiDensity(Layout, Settings, Report);
	Score += 25.0f * CheckRoadReach(Layout, Graph, Settings, Report);
	Score += 20.0f * CheckTerrainFit(Layout, Terrain, Settings, Report);
	Score += 10.0f * CheckStreamingSafety(Layout, Atlas, Settings, Report);
	Score += 15.0f * CheckPlayDensity(Layout, Graph, Settings, Report);

	Report.Score = Score;
	Report.bPassed = !Report.HasErrors();
	return Report;
}
