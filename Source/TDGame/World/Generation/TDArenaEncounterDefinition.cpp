#include "World/Generation/TDArenaEncounterDefinition.h"

#include "MonsterAI/TDMonsterSpeciesAsset.h"

void UTDArenaEncounterDefinition::GatherSpeciesPaths(TArray<FSoftObjectPath>& OutPaths) const
{
	for (const FTDArenaWave& Wave : Waves)
	{
		for (const FTDArenaSpawnEntry& Entry : Wave.Entries)
		{
			const FSoftObjectPath SpeciesPath = Entry.Species.ToSoftObjectPath();
			if (SpeciesPath.IsValid())
			{
				OutPaths.AddUnique(SpeciesPath);
			}
		}
	}
}

bool UTDArenaEncounterDefinition::IsBossWave(const int32 WaveIndex) const
{
	if (!Waves.IsValidIndex(WaveIndex))
	{
		return false;
	}
	return Waves[WaveIndex].Entries.ContainsByPredicate([](const FTDArenaSpawnEntry& Entry) { return Entry.bShowsBossHealthBar; });
}

int32 UTDArenaEncounterDefinition::CountMonstersInWave(const int32 WaveIndex) const
{
	if (!Waves.IsValidIndex(WaveIndex))
	{
		return 0;
	}
	int32 MonsterCount = 0;
	for (const FTDArenaSpawnEntry& Entry : Waves[WaveIndex].Entries)
	{
		MonsterCount += Entry.Species.IsNull() ? 0 : FMath::Max(Entry.Count, 0);
	}
	return MonsterCount;
}
