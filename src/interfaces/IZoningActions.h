#pragma once
#include "simulation_types.h"

class IZoningActions {
public:
    virtual ~IZoningActions() = default;
    virtual void        placeZone(int x, int z, ZoneType t, DensityTier tier,
                                  int earthworksCost = 0) = 0;
    virtual void        placeRoad(int x, int z,
                                  int earthworksCost = 0) = 0;
    virtual void        demolishTile(int x, int z) = 0;
    virtual void        undoLastAction() = 0;
    virtual void        placeServiceBuilding(int x, int z,
                                             ServiceBuildingType type,
                                             int earthworksCost = 0) = 0;
    virtual QueryResult queryTile(int x, int z) const = 0;
    virtual bool        isWithinRoadRange(int x, int z,
                                          DensityTier tier) const = 0;
    virtual bool        hasUndoPendingAction()       const = 0;
    virtual double      getUndoExpiryTimeSeconds()   const = 0;
};
