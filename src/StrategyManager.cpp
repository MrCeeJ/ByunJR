#include "ByunJRBot.h"
#include "common/BotAssert.h"
#include "common/Common.h"
#include "StrategyManager.h"
#include "util/JSONTools.h"
#include "util/Util.h"

Strategy::Strategy()
{

}

Strategy::Strategy(const std::string & name, const sc2::Race & race, const BuildOrder & buildOrder)
    : name(name)
    , race(race)
    , buildOrder(buildOrder)
    , wins(0)
    , losses(0)
{

}

// constructor
StrategyManager::StrategyManager(ByunJRBot & bot)
    : bot_(bot)
    , initial_scout_set_(false)
    , second_proxy_worker_set_(false)
{
}

void StrategyManager::OnStart()
{
    ReadStrategyFile(bot_.Config().ConfigFileLocation);
}

// This strategy code is only for Terran. 
// This code will not function correctly if playing other races.
void StrategyManager::OnFrame()
{
    // Update variables that we will need later. 
    bases_safe_ = AreBasesSafe();

    HandleUnitAssignments();

    for (const auto & unit : bot_.InformationManager().UnitInfo().GetUnits(PlayerArrayIndex::Self))
    {
        // Find all the depots and perform some actions on them. 
        if (Util::IsBuilding(unit->unit_type))
        {
            // If the depot may die, go repair it. 
            if (unit->health != unit->health_max)
                Micro::SmartRepairWithSCVCount(unit, 2, bot_);

            if (unit->health < unit->health_max/3)
            {
                bot_.Actions()->UnitCommand(unit, sc2::ABILITY_ID::LIFT);
            }
            else
            {
            //    bot_.Actions()->UnitCommand(unit, sc2::ABILITY_ID::LAND,unit->pos);
            }
        }
    }
}

// assigns units to various managers
void StrategyManager::HandleUnitAssignments()
{
    SetScoutUnits();

    // Repair any damaged supply depots. If our base is safe, lower the wall. Otherwise, raise the wall. 
    for (const auto & unit : bot_.InformationManager().UnitInfo().GetUnits(PlayerArrayIndex::Self))
    {
        // Find all the depots and perform some actions on them. 
        if (Util::IsSupplyProvider(unit))
        {
            // If the depot may die, go repair it. 
            if (unit->health != unit->health_max)
                Micro::SmartRepairWithSCVCount(unit, 2, bot_);

            if (bases_safe_)
            {
                bot_.Actions()->UnitCommand(unit, sc2::ABILITY_ID::MORPH_SUPPLYDEPOT_LOWER);
            }
            else
            {
                bot_.Actions()->UnitCommand(unit, sc2::ABILITY_ID::MORPH_SUPPLYDEPOT_RAISE);
            }
        }
    }
}

void StrategyManager::SetScoutUnits()
{
    // if we haven't set a scout unit, do it
    if (bot_.InformationManager().UnitInfo().GetScouts().empty() && !initial_scout_set_)
    {
        // Should we send the initial scout?
        if (ShouldSendInitialScout())
        {
            // grab the closest worker to the supply provider to send to scout
            const ::UnitInfo * worker_scout = bot_.InformationManager().GetClosestUnitInfoWithJob(bot_.GetStartLocation(), UnitMission::Minerals);

            // if we find a worker (which we should) add it to the scout units
            if (worker_scout)
            {
                bot_.InformationManager().UnitInfo().SetJob(worker_scout->unit, UnitMission::Proxy);
                initial_scout_set_ = true;
            }
        }
        // Is it time to send the worker to go build the second barracks?
        if (ShouldSendSecondProxyWorker())
        {
            // grab the closest worker to the supply provider to send to scout
            const ::UnitInfo * proxy_worker = bot_.InformationManager().GetClosestUnitInfoWithJob(bot_.GetStartLocation(), UnitMission::Minerals);

            // if we find a worker (which we should) add it to the scout units
            if (proxy_worker)
            {
                bot_.InformationManager().UnitInfo().SetJob(proxy_worker->unit, UnitMission::Proxy);
                second_proxy_worker_set_ = true;
            }
        }
    }
}

bool StrategyManager::ShouldSendSecondProxyWorker() const
{
    if (Util::GetGameTimeInSeconds(bot_) > 20 && !second_proxy_worker_set_)
        return true;
    return false;
}

bool StrategyManager::ShouldSendInitialScout() const
{
    return false;
    switch (bot_.InformationManager().GetPlayerRace(PlayerArrayIndex::Self))
    {
        case sc2::Race::Terran:  return bot_.InformationManager().UnitInfo().GetUnitTypeCount(PlayerArrayIndex::Self, sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT, true) > 0;
        case sc2::Race::Protoss: return bot_.InformationManager().UnitInfo().GetUnitTypeCount(PlayerArrayIndex::Self, sc2::UNIT_TYPEID::PROTOSS_PYLON, true) > 0;
        case sc2::Race::Zerg:    return bot_.InformationManager().UnitInfo().GetUnitTypeCount(PlayerArrayIndex::Self, sc2::UNIT_TYPEID::ZERG_SPAWNINGPOOL, true) > 0;
        default: return false;
    }
}

bool StrategyManager::AreBasesSafe()
{
    for (const auto & enemy_unit : bot_.InformationManager().UnitInfo().GetUnits(PlayerArrayIndex::Enemy))
    {
        for (const auto & potential_base : bot_.InformationManager().UnitInfo().GetUnits(PlayerArrayIndex::Self))
        {
            if( Util::IsTownHall(potential_base)
             && Util::DistSq(potential_base->pos, enemy_unit->pos) < (30*30))
            {
                return false;
            }
        }
    }
    return true;
}

const BuildOrder & StrategyManager::GetOpeningBookBuildOrder() const
{
    const auto build_order_it = strategies_.find(bot_.Config().StrategyName);

    // look for the build order in the build order map
    if (build_order_it != std::end(strategies_))
    {
        return (*build_order_it).second.buildOrder;
    }
    else
    {
        BOT_ASSERT(false, "Strategy not found: %s, returning empty initial build order", bot_.Config().StrategyName.c_str());
        return empty_build_order_;
    }
}

bool StrategyManager::ShouldExpandNow() const
{
    const int num_bases = bot_.InformationManager().UnitInfo().GetUnitTypeCount(PlayerArrayIndex::Self, sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER)
                    + bot_.InformationManager().UnitInfo().GetUnitTypeCount(PlayerArrayIndex::Self, sc2::UNIT_TYPEID::TERRAN_ORBITALCOMMAND);
    if (bot_.Observation()->GetMinerals() > 400 && num_bases < 3 && bases_safe_)
        return true;
    return false;
}

void StrategyManager::AddStrategy(const std::string & name, const Strategy & strategy)
{
    strategies_[name] = strategy;
}

UnitPairVector StrategyManager::GetBuildOrderGoal() const
{
    return std::vector<UnitPair>();
}

UnitPairVector StrategyManager::GetProtossBuildOrderGoal() const
{
    return std::vector<UnitPair>();
}

UnitPairVector StrategyManager::GetTerranBuildOrderGoal() const
{
    return std::vector<UnitPair>();
}

UnitPairVector StrategyManager::GetZergBuildOrderGoal() const
{
    return std::vector<UnitPair>();
}

void StrategyManager::ReadStrategyFile(const std::string & filename)
{
    const sc2::Race race = bot_.InformationManager().GetPlayerRace(PlayerArrayIndex::Self);
    std::string our_race = Util::GetStringFromRace(race);
    std::string config = bot_.Config().RawConfigString;
    rapidjson::Document doc;
    const bool parsing_failed = doc.Parse(config.c_str()).HasParseError();
    if (parsing_failed)
    {
        std::cerr << "ParseStrategy could not find file: " << filename << ", shutting down.\n";
        return;
    }

    // Parse the Strategy Options
    if (doc.HasMember("Strategy") && doc["Strategy"].IsObject())
    {
        const rapidjson::Value & strategy = doc["Strategy"];

        // read in the various strategic elements
        JSONTools::ReadBool("ScoutHarassEnemy", strategy, bot_.Config().ScoutHarassEnemy);
        JSONTools::ReadString("ReadDirectory", strategy, bot_.Config().ReadDir);
        JSONTools::ReadString("WriteDirectory", strategy, bot_.Config().WriteDir);

        // if we have set a strategy for the current race, use it
        if (strategy.HasMember(our_race.c_str()) && strategy[our_race.c_str()].IsString())
        {
            bot_.Config().StrategyName = strategy[our_race.c_str()].GetString();
        }

        // check if we are using an enemy specific strategy
        JSONTools::ReadBool("UseEnemySpecificStrategy", strategy, bot_.Config().UseEnemySpecificStrategy);
        if (bot_.Config().UseEnemySpecificStrategy && strategy.HasMember("EnemySpecificStrategy") && strategy["EnemySpecificStrategy"].IsObject())
        {
            // TODO: Figure out enemy name
            const std::string enemy_name = "ENEMY NAME";
            const rapidjson::Value & specific = strategy["EnemySpecificStrategy"];

            // check to see if our current enemy name is listed anywhere in the specific strategies
            if (specific.HasMember(enemy_name.c_str()) && specific[enemy_name.c_str()].IsObject())
            {
                const rapidjson::Value & enemy_strategies = specific[enemy_name.c_str()];

                // if that enemy has a strategy listed for our current race, use it
                if (enemy_strategies.HasMember(our_race.c_str()) && enemy_strategies[our_race.c_str()].IsString())
                {
                    bot_.Config().StrategyName = enemy_strategies[our_race.c_str()].GetString();
                    bot_.Config().FoundEnemySpecificStrategy = true;
                }
            }
        }

        // Parse all the Strategies
        if (strategy.HasMember("Strategies") && strategy["Strategies"].IsObject())
        {
            const rapidjson::Value & strategies = strategy["Strategies"];
            for (auto itr = strategies.MemberBegin(); itr != strategies.MemberEnd(); ++itr)
            {
                const std::string &         name = itr->name.GetString();
                const rapidjson::Value &    val  = itr->value;

                sc2::Race strategy_race;
                if (val.HasMember("Race") && val["Race"].IsString())
                {
                    strategy_race = Util::GetRaceFromString(val["Race"].GetString());
                }
                else
                {
                    BOT_ASSERT(false, "Strategy must have a Race string: %s", name.c_str());
                    continue;
                }

                BuildOrder build_order(strategy_race);
                if (val.HasMember("OpeningBuildOrder") && val["OpeningBuildOrder"].IsArray())
                {
                    const rapidjson::Value & build = val["OpeningBuildOrder"];

                    for (rapidjson::SizeType b(0); b < build.Size(); ++b)
                    {
                        if (build[b].IsString())
                        {
                            const sc2::UnitTypeID type_id = Util::GetUnitTypeIDFromName(bot_.Observation(), build[b].GetString());

                            build_order.Add(type_id);
                        }
                        else
                        {
                            BOT_ASSERT(false, "Build order item must be a string %s", name.c_str());
                            continue;
                        }
                    }
                }

                AddStrategy(name, Strategy(name, strategy_race, build_order));
            }
        }
    }
}