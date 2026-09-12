#include "TDWorldGenSettings.h"

const UTDWorldGenSettings* UTDWorldGenSettings::Get()
{
	return GetDefault<UTDWorldGenSettings>();
}
