#include "Building.h"

Building::Building(const sc2::UnitTypeID t)
    : finalPosition     (0,0)
    , type              (t)
    , buildingUnit      (0)
    , builderUnit       (0)
    , status            (BuildingStatus::Unassigned)
    , buildCommandGiven (false)
    , underConstruction (false) 
{}

bool Building::operator == (const Building& b) 
{
    // buildings are equal if their worker unit and building unit are equal
    return      (b.buildingUnit == buildingUnit) 
             && (b.builderUnit  == builderUnit) 
             && (b.finalPosition.x == finalPosition.x)
             && (b.finalPosition.y == finalPosition.y);
}