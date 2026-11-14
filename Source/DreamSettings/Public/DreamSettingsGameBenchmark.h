#pragma once
#include "DreamSettingsModel.h"

class ULocalPlayer;

/** Host-owned gameplay benchmark, separate from scalability's quality recommendation.
 * Calls are on the game thread. Start accepts/rejects a launch synchronously; queue
 * travel or external UI work until it returns, so the settings page can close first.
 * The host owns the benchmark scene, asynchronous work, results and return path. */
class DREAMSETTINGS_API IDreamSettingsGameBenchmark
{
public:
    virtual ~IDreamSettingsGameBenchmark()=default;
    virtual FDreamSettingsResult Availability(ULocalPlayer* Player) const=0;
    virtual FDreamSettingsResult Start(ULocalPlayer* Player)=0;
};
