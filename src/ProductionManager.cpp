#include "ProductionManager.h"
#include "Util.h"
#include "ByunJRBot.h"
#include "Micro.h"

ProductionManager::ProductionManager(ByunJRBot & bot)
    : m_bot             (bot)
    , m_buildingManager (bot)
    , m_queue           (bot)
{

}

void ProductionManager::setBuildOrder(const BuildOrder & buildOrder)
{
    m_queue.clearAll();

    for (size_t i(0); i<buildOrder.size(); ++i)
    {
        m_queue.queueAsLowestPriority(buildOrder[i], true);
    }
}


void ProductionManager::onStart()
{
    m_buildingManager.onStart();
    setBuildOrder(m_bot.Strategy().getOpeningBookBuildOrder());
}

void ProductionManager::onFrame()
{
    // check the _queue for stuff we can build
    manageBuildOrderQueue();

    // TODO: if nothing is currently building, get a new goal from the strategy manager
    // TODO: detect if there's a build order deadlock once per second
    // TODO: triggers for game things like cloaked units etc

    m_buildingManager.onFrame();
    drawProductionInformation();
}

// on unit destroy
void ProductionManager::onUnitDestroy(const sc2::Unit & unit)
{
    // TODO: might have to re-do build order if a vital unit died
}

void ProductionManager::manageBuildOrderQueue()
{
    // if there is nothing in the queue, oh well
    if (m_queue.isEmpty())
    {
        return;
    }

    // the current item to be used
    BuildOrderItem & currentItem = m_queue.getHighestPriorityItem();

    // while there is still something left in the queue
    while (!m_queue.isEmpty())
    {
        // this is the unit which can produce the currentItem
        UnitTag producer = getProducer(currentItem.type);

        // check to see if we can make it right now
        bool canMake = canMakeNow(producer, currentItem.type);

        // TODO: if it's a building and we can't make it yet, predict the worker movement to the location

        // if we can make the current item
        if (producer && canMake)
        {
            // create it and remove it from the _queue
            create(producer, currentItem);
            m_queue.removeCurrentHighestPriorityItem();

            // don't actually loop around in here
            break;
        }
        // otherwise, if we can skip the current item
        else if (m_queue.canSkipItem())
        {
            // skip it
            m_queue.skipItem();

            // and get the next one
            currentItem = m_queue.getNextHighestPriorityItem();
        }
        else
        {
            // so break out
            break;
        }
    }
}

UnitTag ProductionManager::getProducer(sc2::UnitTypeID t, sc2::Point2D closestTo)
{
    // TODO: get the type of unit that builds this
    sc2::UnitTypeID producerType = Util::WhatBuilds(t);

    // make a set of all candidate producers
    std::vector<UnitTag> candidateProducers;
    for (auto & unit : m_bot.UnitInfo().getUnits(Players::Self))
    {
        // reasons a unit can not train the desired type
        if (unit.unit_type != producerType) { continue; }
        if (unit.build_progress < 1.0f) { continue; }
        if (Util::IsBuilding(producerType) && unit.orders.size() > 0) { continue; }
        // TODO: if unit is not powered continue
        if (unit.is_flying) { continue; }

        // TODO: if the type is an addon, some special cases
        // TODO: if the type requires an addon and the producer doesn't have one

        // if we haven't cut it, add it to the set of candidates
        candidateProducers.push_back(unit.tag);
    }

    return getClosestUnitToPosition(candidateProducers, closestTo);
}

UnitTag ProductionManager::getClosestUnitToPosition(const std::vector<UnitTag> & units, sc2::Point2D closestTo)
{
    if (units.size() == 0)
    {
        return 0;
    }

    // if we don't care where the unit is return the first one we have
    if (closestTo.x == 0 && closestTo.y == 0)
    {
        return units[0];
    }

    UnitTag closestUnit = 0;
    double minDist = std::numeric_limits<double>::max();

    for (auto & unit : units)
    {
        double distance = Util::Dist(m_bot.GetUnit(unit)->pos, closestTo);
        if (!closestUnit || distance < minDist)
        {
            closestUnit = unit;
            minDist = distance;
        }
    }

    return closestUnit;
}

// this function will check to see if all preconditions are met and then create a unit
void ProductionManager::create(UnitTag producer, BuildOrderItem & item)
{
    if (!producer)
    {
        return;
    }

    sc2::UnitTypeID t = item.type;

    // if we're dealing with a building
    // TODO: deal with morphed buildings & addons
    if (Util::IsBuilding(t))
    {
        // send the building task to the building manager
        if (t == sc2::UNIT_TYPEID::TERRAN_BARRACKS)
        {
            //TODO: DELETE THE MEMORY LEAK
            sc2::Point2D * proxyLocation = &m_bot.GetProxyLocation();
            std::cout << "PROXYLOC" << proxyLocation->x << "x " << proxyLocation->y << "y " << std::endl;
            m_buildingManager.addBuildingTask(t, *proxyLocation);
        }
        else
        {
            m_buildingManager.addBuildingTask(t, m_bot.GetStartLocation());
        }
    }
    // if we're dealing with a non-building unit
    else
    {
        Micro::SmartTrain(producer, t, m_bot);
    }
}

bool ProductionManager::canMakeNow(UnitTag producerTag, sc2::UnitTypeID type)
{
    if (!meetsReservedResources(type))
    {
        return false;
    }

    sc2::AvailableAbilities available_abilities = m_bot.Query()->GetAbilitiesForUnit(producerTag);

    // quick check if the unit can't do anything it certainly can't build the thing we want
    if (available_abilities.abilities.empty())
    {
        return false;
    }
    else
    {
        // check to see if one of the unit's available abilities matches the build ability type
        sc2::AbilityID buildTypeAbility = Util::UnitTypeIDToAbilityID(type);
        for (const sc2::AvailableAbility & available_ability : available_abilities.abilities)
        {
            if (available_ability.ability_id == buildTypeAbility)
            {
                return true;
            }
        }
    }

    return false;
}

bool ProductionManager::detectBuildOrderDeadlock()
{
    // TODO: detect build order deadlocks here
    return false;
}

int ProductionManager::getFreeMinerals()
{
    return m_bot.Observation()->GetMinerals() - m_buildingManager.getReservedMinerals();
}

int ProductionManager::getFreeGas()
{
    return m_bot.Observation()->GetVespene() - m_buildingManager.getReservedGas();
}

// return whether or not we meet resources, including building reserves
bool ProductionManager::meetsReservedResources(sc2::UnitTypeID type)
{
    // return whether or not we meet the resources
    return (Util::GetUnitTypeMineralPrice(type, m_bot) <= getFreeMinerals()) && (Util::GetUnitTypeGasPrice(type, m_bot) <= getFreeGas());
}

void ProductionManager::drawProductionInformation()
{
    if (!m_bot.Config().DrawProductionInfo)
    {
        return;
    }

    std::stringstream ss;
    ss << "Production Information\n\n";

    for (auto & unit : m_bot.UnitInfo().getUnits(Players::Self))
    {
        if (unit.build_progress < 1.0f)
        {
            //ss << sc2::UnitTypeToName(unit.unit_type) << " " << unit.build_progress << "\n";
        }
    }

    ss << m_queue.getQueueInformation();

    m_bot.Map().drawTextScreen(sc2::Point2D(0.01f, 0.01f), ss.str(), sc2::Colors::Yellow);
}
