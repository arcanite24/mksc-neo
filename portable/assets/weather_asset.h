#ifndef PORTABLE_WEATHER_ASSET_H
#define PORTABLE_WEATHER_ASSET_H

#include "asset_pack.h"
#include "native_image.h"
#include "snow_weather.h"

typedef struct
{
    PortableSnowWeatherRules rules;
    PortableNativeImage images[3];
} PortableWeatherAssetView;

int portable_weather_asset_open(const PortableAssetSectionView *section,
    PortableWeatherAssetView *out);
/* Optional course profile; missing old-pack data succeeds with NULL. */
int portable_weather_asset_load(const PortableAssetPackView *pack,uint32_t course_id,
    PortableWeatherAssetView *storage,const PortableWeatherAssetView **out);

#endif
