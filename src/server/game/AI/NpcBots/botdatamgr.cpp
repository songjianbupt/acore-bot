#include "botdatamgr.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "StringConvert.h"
#include "WorldDatabase.h"
/*
Npc Bot Data Manager by Trickerer (onlysuffering@gmail.com)
NpcBots DB Data management
%Complete: ???
*/

typedef std::unordered_map<uint32 /*entry*/, NpcBotData*> NpcBotDataMap;
typedef std::unordered_map<uint32 /*entry*/, NpcBotAppearanceData*> NpcBotAppearanceDataMap;
typedef std::unordered_map<uint32 /*entry*/, NpcBotExtras*> NpcBotExtrasMap;
NpcBotDataMap _botsData;
NpcBotAppearanceDataMap _botsAppearanceData;
NpcBotExtrasMap _botsExtras;
NpcBotRegistry _existingBots;

bool allBotsLoaded = false;

std::shared_mutex* BotDataMgr::GetLock()
{
    static std::shared_mutex _lock;
    return &_lock;
}

bool BotDataMgr::AllBotsLoaded()
{
    return allBotsLoaded;
}

void BotDataMgr::LoadNpcBots(bool spawn)
{
    if (allBotsLoaded)
        return;

    uint32 botoldMSTime = getMSTime();

    LOG_INFO("server.loading", "Starting NpcBot system...");

    Field* field;
    uint8 index;

    //                                                      1       2     3     4     5          6
    QueryResult result = WorldDatabase.Query("SELECT entry, gender, skin, face, hair, haircolor, features FROM creature_template_npcbot_appearance");
    if (result)
    {
        do
        {
            field = result->Fetch();
            index = 0;
            uint32 entry = field[  index].Get<uint32>();

            NpcBotAppearanceData* appearanceData = new NpcBotAppearanceData();
            appearanceData->gender =    field[++index].GetUInt8();
            appearanceData->skin =      field[++index].GetUInt8();
            appearanceData->face =      field[++index].GetUInt8();
            appearanceData->hair =      field[++index].GetUInt8();
            appearanceData->haircolor = field[++index].GetUInt8();
            appearanceData->features =  field[++index].GetUInt8();

            _botsAppearanceData[entry] = appearanceData;

        } while (result->NextRow());

        LOG_INFO("server.loading", ">> Bot appearance data loaded");
    }
    else
        LOG_INFO("server.loading", ">> Bots appearance data is not loaded. Table `creature_template_npcbot_appearance` is empty!");

    //                                          1      2
    result = WorldDatabase.Query("SELECT entry, class, race FROM creature_template_npcbot_extras");
    if (result)
    {
        do
        {
            field = result->Fetch();
            index = 0;
            uint32 entry =      field[  index].Get<uint32>();

            NpcBotExtras* extras = new NpcBotExtras();
            extras->bclass =    field[++index].GetUInt8();
            extras->race =      field[++index].GetUInt8();

            _botsExtras[entry] = extras;

        } while (result->NextRow());

        LOG_INFO("server.loading", ">> Bot race data loaded");
    }
    else
        LOG_INFO("server.loading", ">> Bots race data is not loaded. Table `creature_template_npcbot_extras` is empty!");

    //                                       0      1      2      3     4        5          6          7          8          9               10          11          12         13
    result = CharacterDatabase.Query("SELECT entry, owner, roles, spec, faction, equipMhEx, equipOhEx, equipRhEx, equipHead, equipShoulders, equipChest, equipWaist, equipLegs, equipFeet,"
    //   14          15          16         17         18            19            20             21             22         23
        "equipWrist, equipHands, equipBack, equipBody, equipFinger1, equipFinger2, equipTrinket1, equipTrinket2, equipNeck, spells_disabled FROM characters_npcbot");

    if (!result)
    {
        LOG_INFO("server.loading", ">> Loaded 0 npcbots. Table `characters_npcbot` is empty!");
        allBotsLoaded = true;
        return;
    }

    uint32 botcounter = 0;
    uint32 datacounter = 0;
    std::list<uint32> botgrids;
    QueryResult infores;
    CreatureTemplate const* proto;
    NpcBotData* botData;
    std::list<uint32> entryList;

    do
    {
        field = result->Fetch();
        index = 0;
        uint32 entry =          field[  index].Get<uint32>();

        //load data
        botData = new NpcBotData(0, 0);
        botData->owner =        field[++index].Get<uint32>();
        botData->roles =        field[++index].Get<uint32>();
        botData->spec =         field[++index].GetUInt8();
        botData->faction =      field[++index].Get<uint32>();

        for (uint8 i = BOT_SLOT_MAINHAND; i != BOT_INVENTORY_SIZE; ++i)
            botData->equips[i] = field[++index].Get<uint32>();

        if (char const* disabled_spells_str = field[++index].GetCString())
        {
            std::vector<std::string_view> tok = Acore::Tokenize(disabled_spells_str, ' ', false);
            for (std::vector<std::string_view>::size_type i = 0; i != tok.size(); ++i)
                botData->disabled_spells.insert(*(Acore::StringTo<uint32>(tok[i])));
        }

        entryList.push_back(entry);
        _botsData[entry] = botData;
        ++datacounter;

    } while (result->NextRow());

    LOG_INFO("server.loading", ">> Loaded %u bot data entries", datacounter);

    if (!spawn)
    {
        allBotsLoaded = true;
        return;
    }

    for (std::list<uint32>::const_iterator itr = entryList.begin(); itr != entryList.end(); ++itr)
    {
        uint32 entry = *itr;
        proto = sObjectMgr->GetCreatureTemplate(entry);
        if (!proto)
        {
            LOG_ERROR("server.loading", "Cannot find creature_template entry for npcbot (id: %u)!", entry);
            continue;
        }
        //                                     1     2    3           4            5           6
        infores = WorldDatabase.Query("SELECT guid, map, position_x, position_y"/*, position_z, orientation*/" FROM creature WHERE id = %u", entry);
        if (!infores)
        {
            LOG_ERROR("server.loading", "Cannot spawn npcbot %s (id: %u), not found in `creature` table!", proto->Name.c_str(), entry);
            continue;
        }

        field = infores->Fetch();
        uint32 tableGuid = field[0].Get<uint32>();
        uint32 mapId = uint32(field[1].GetUInt16());
        float pos_x = field[2].GetFloat();
        float pos_y = field[3].GetFloat();
        //float pos_z = field[4].GetFloat();
        //float ori = field[5].GetFloat();

        CellCoord c = Acore::ComputeCellCoord(pos_x, pos_y);
        GridCoord g = Acore::ComputeGridCoord(pos_x, pos_y);
        ASSERT(c.IsCoordValid() && "Invalid Cell coord!");
        ASSERT(g.IsCoordValid() && "Invalid Grid coord!");
        Map* map = sMapMgr->CreateBaseMap(mapId);
        map->LoadGrid(pos_x, pos_y);

        typedef std::unordered_multimap<uint32, Creature*>::iterator SpawnIter;
        std::pair<SpawnIter, SpawnIter> creBounds = map->GetCreatureBySpawnIdStore().equal_range(tableGuid);
        if (creBounds.first == map->GetCreatureBySpawnIdStore().end())
        {
            LOG_ERROR("server.loading", "bot %u is not in spawns list, consider re-spawning it!", entry);
            continue;
        }

        ObjectGuid Guid(HighGuid::Unit, entry, tableGuid);
        LOG_DEBUG("server.loading", "bot %u: spawnId %u, full %s", entry, tableGuid, Guid.ToString().c_str());
        Creature* bot = map->GetCreature(Guid);
        if (!bot) //not in map, use storage
            bot = creBounds.first->second;
        ASSERT(bot);
        if (!bot->FindMap())
            LOG_ERROR("server.loading", "bot %u is not in map!", entry);
        if (!bot->IsInWorld())
            LOG_ERROR("server.loading", "bot %u is not in world!", entry);
        if (!bot->IsAlive())
        {
            LOG_ERROR("server.loading", "bot %u is dead, respawning!", entry);
            bot->Respawn();
        }

        LOG_DEBUG("server.loading", ">> Spawned npcbot %s (id: %u, map: %u, grid: %u, cell: %u)", proto->Name.c_str(), entry, mapId, g.GetId(), c.GetId());
        botgrids.push_back(g.GetId());
        ++botcounter;
    }

    botgrids.sort();
    botgrids.unique();
    LOG_INFO("server.loading", ">> Spawned %u npcbot(s) within %u grid(s) in %u ms", botcounter, uint32(botgrids.size()), GetMSTimeDiffToNow(botoldMSTime));

    allBotsLoaded = true;
}

void BotDataMgr::AddNpcBotData(uint32 entry, uint32 roles, uint8 spec, uint32 faction)
{
    //botData must be allocated explicitly
    NpcBotDataMap::iterator itr = _botsData.find(entry);
    if (itr == _botsData.end())
    {
        NpcBotData* botData = new NpcBotData(roles, faction, spec);
        _botsData[entry] = botData;

        CharacterDatabasePreparedStatement* bstmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NPCBOT);
        //"INSERT INTO characters_npcbot (entry, roles, spec, faction) VALUES (?, ?, ?, ?)", CONNECTION_ASYNC);
        bstmt->SetData<uint32>(0, entry);
        bstmt->SetData<uint32>(1, roles);
        bstmt->setUInt8(2, spec);
        bstmt->SetData<uint32>(3, faction);
        CharacterDatabase.Execute(bstmt);

        return;
    }

    LOG_ERROR("sql.sql", "BotMgr::AddNpcBotData(): trying to add new data but entry already exists! entry = %u", entry);
}
NpcBotData const* BotDataMgr::SelectNpcBotData(uint32 entry)
{
    NpcBotDataMap::const_iterator itr = _botsData.find(entry);
    return itr != _botsData.end() ? itr->second : nullptr;
}
void BotDataMgr::UpdateNpcBotData(uint32 entry, NpcBotDataUpdateType updateType, void* data)
{
    NpcBotDataMap::iterator itr = _botsData.find(entry);
    if (itr == _botsData.end())
        return;

    CharacterDatabasePreparedStatement* bstmt;
    switch (updateType)
    {
        case NPCBOT_UPDATE_OWNER:
            itr->second->owner = *(uint32*)(data);
            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NPCBOT_OWNER);
            //"UPDATE characters_npcbot SET owner = ? WHERE entry = ?", CONNECTION_ASYNC
            bstmt->SetData<uint32>(0, itr->second->owner);
            bstmt->SetData<uint32>(1, entry);
            CharacterDatabase.Execute(bstmt);
            break;
        case NPCBOT_UPDATE_ROLES:
            itr->second->roles = *(uint32*)(data);
            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NPCBOT_ROLES);
            //"UPDATE character_npcbot SET roles = ? WHERE entry = ?", CONNECTION_ASYNC
            bstmt->SetData<uint32>(0, itr->second->roles);
            bstmt->SetData<uint32>(1, entry);
            CharacterDatabase.Execute(bstmt);
            break;
        case NPCBOT_UPDATE_SPEC:
            itr->second->spec = *(uint8*)(data);
            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NPCBOT_SPEC);
            //"UPDATE characters_npcbot SET spec = ? WHERE entry = ?", CONNECTION_ASYNCH
            bstmt->setUInt8(0, itr->second->spec);
            bstmt->SetData<uint32>(1, entry);
            CharacterDatabase.Execute(bstmt);
            break;
        case NPCBOT_UPDATE_FACTION:
            itr->second->faction = *(uint32*)(data);
            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NPCBOT_FACTION);
            //"UPDATE characters_npcbot SET faction = ? WHERE entry = ?", CONNECTION_ASYNCH
            bstmt->SetData<uint32>(0, itr->second->faction);
            bstmt->SetData<uint32>(1, entry);
            CharacterDatabase.Execute(bstmt);
            break;
        case NPCBOT_UPDATE_DISABLED_SPELLS:
        {
            NpcBotData::DisabledSpellsContainer const* spells = (NpcBotData::DisabledSpellsContainer const*)(data);
            std::ostringstream ss;
            for (NpcBotData::DisabledSpellsContainer::const_iterator citr = spells->begin(); citr != spells->end(); ++citr)
                ss << (*citr) << ' ';

            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NPCBOT_DISABLED_SPELLS);
            //"UPDATE characters_npcbot SET spells_disabled = ? WHERE entry = ?", CONNECTION_ASYNCH
            bstmt->setString(0, ss.str());
            bstmt->SetData<uint32>(1, entry);
            CharacterDatabase.Execute(bstmt);
            break;
        }
        case NPCBOT_UPDATE_EQUIPS:
        {
            Item** items = (Item**)(data);

            int8 id = 1;
            EquipmentInfo const* einfo = sObjectMgr->GetEquipmentInfo(entry, id);

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NPCBOT_EQUIP);
            //"UPDATE character_npcbot SET equipMhEx = ?, equipOhEx = ?, equipRhEx = ?, equipHead = ?, equipShoulders = ?, equipChest = ?, equipWaist = ?, equipLegs = ?,
            //equipFeet = ?, equipWrist = ?, equipHands = ?, equipBack = ?, equipBody = ?, equipFinger1 = ?, equipFinger2 = ?, equipTrinket1 = ?, equipTrinket2 = ?, equipNeck = ? WHERE entry = ?", CONNECTION_ASYNC
            CharacterDatabasePreparedStatement* stmt;
            uint8 k;
            for (k = BOT_SLOT_MAINHAND; k != BOT_INVENTORY_SIZE; ++k)
            {
                itr->second->equips[k] = items[k] ? items[k]->GetGUID().GetCounter() : 0;
                if (Item const* botitem = items[k])
                {
                    bool standard = false;
                    for (uint8 i = 0; i != MAX_EQUIPMENT_ITEMS; ++i)
                    {
                        if (einfo->ItemEntry[i] == botitem->GetEntry())
                        {
                            itr->second->equips[k] = 0;
                            bstmt->SetData<uint32>(k, 0);
                            standard = true;
                            break;
                        }
                    }
                    if (standard)
                        continue;

                    uint8 index = 0;
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_ITEM_INSTANCE);
                    //REPLACE INTO item_instance (itemEntry, owner_guid, creatorGuid, giftCreatorGuid, count, duration, charges, flags, enchantments, randomPropertyId, durability, playedTime, text, guid)
                    //VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC : 0-13
                    stmt->SetData<uint32>(  index, botitem->GetEntry());
                    stmt->SetData<uint32>(++index, botitem->GetOwnerGUID().GetCounter());
                    stmt->SetData<uint32>(++index, botitem->GetGuidValue(ITEM_FIELD_CREATOR).GetCounter());
                    stmt->SetData<uint32>(++index, botitem->GetGuidValue(ITEM_FIELD_GIFTCREATOR).GetCounter());
                    stmt->SetData<uint32>(++index, botitem->GetCount());
                    stmt->SetData<uint32>(++index, botitem->GetUInt32Value(ITEM_FIELD_DURATION));

                    std::ostringstream ssSpells;
                    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                        ssSpells << botitem->GetSpellCharges(i) << ' ';
                    stmt->setString(++index, ssSpells.str());

                    stmt->SetData<uint32>(++index, botitem->GetUInt32Value(ITEM_FIELD_FLAGS));

                    std::ostringstream ssEnchants;
                    for (uint8 i = 0; i < MAX_ENCHANTMENT_SLOT; ++i)
                    {
                        ssEnchants << botitem->GetEnchantmentId(EnchantmentSlot(i)) << ' ';
                        ssEnchants << botitem->GetEnchantmentDuration(EnchantmentSlot(i)) << ' ';
                        ssEnchants << botitem->GetEnchantmentCharges(EnchantmentSlot(i)) << ' ';
                    }
                    stmt->setString(++index, ssEnchants.str());

                    stmt->setInt16 (++index, botitem->GetItemRandomPropertyId());
                    stmt->setUInt16(++index, botitem->GetUInt32Value(ITEM_FIELD_DURABILITY));
                    stmt->SetData<uint32>(++index, botitem->GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME));
                    stmt->setString(++index, botitem->GetText());
                    stmt->SetData<uint32>(++index, botitem->GetGUID().GetCounter());

                    trans->Append(stmt);

                    Item::DeleteFromInventoryDB(trans, botitem->GetGUID().GetCounter()); //prevent duplicates

                    bstmt->SetData<uint32>(k, botitem->GetGUID().GetCounter());
                }
                else
                    bstmt->SetData<uint32>(k, uint32(0));
            }

            bstmt->SetData<uint32>(k, entry);
            trans->Append(bstmt);
            CharacterDatabase.CommitTransaction(trans);
            break;
        }
        case NPCBOT_UPDATE_ERASE:
        {
            NpcBotDataMap::iterator bitr = _botsData.find(entry);
            ASSERT(bitr != _botsData.end());
            delete bitr->second;
            _botsData.erase(bitr);
            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NPCBOT);
            //"DELETE FROM characters_npcbot WHERE entry = ?", CONNECTION_ASYNC
            bstmt->SetData<uint32>(0, entry);
            CharacterDatabase.Execute(bstmt);
            break;
        }
        default:
            LOG_ERROR("sql.sql", "BotDataMgr:UpdateNpcBotData: unhandled updateType %u", uint32(updateType));
            break;
    }
}
void BotDataMgr::UpdateNpcBotDataAll(uint32 playerGuid, NpcBotDataUpdateType updateType, void* data)
{
    CharacterDatabasePreparedStatement* bstmt;
    switch (updateType)
    {
        case NPCBOT_UPDATE_OWNER:
            bstmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NPCBOT_OWNER_ALL);
            //"UPDATE characters_npcbot SET owner = ? WHERE owner = ?", CONNECTION_ASYNC
            bstmt->SetData<uint32>(0, *(uint32*)(data));
            bstmt->SetData<uint32>(1, playerGuid);
            CharacterDatabase.Execute(bstmt);
            break;
        //case NPCBOT_UPDATE_ROLES:
        //case NPCBOT_UPDATE_FACTION:
        //case NPCBOT_UPDATE_EQUIPS:
        default:
            LOG_ERROR("sql.sql", "BotDataMgr:UpdateNpcBotDataAll: unhandled updateType %u", uint32(updateType));
            break;
    }
}

void BotDataMgr::SaveNpcBotStats(NpcBotStats const* stats)
{
    CharacterDatabasePreparedStatement* bstmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_NPCBOT_STATS);
    //"REPLACE INTO characters_npcbot_stats
    //(entry, maxhealth, maxpower, strength, agility, stamina, intellect, spirit, armor, defense,
    //resHoly, resFire, resNature, resFrost, resShadow, resArcane, blockPct, dodgePct, parryPct, critPct,
    //attackPower, spellPower, spellPen, hastePct, hitBonusPct, expertise, armorPenPct) VALUES
    //(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC

    uint32 index = 0;
    bstmt->SetData<uint32>(  index, stats->entry);
    bstmt->SetData<uint32>(++index, stats->maxhealth);
    bstmt->SetData<uint32>(++index, stats->maxpower);
    bstmt->SetData<uint32>(++index, stats->strength);
    bstmt->SetData<uint32>(++index, stats->agility);
    bstmt->SetData<uint32>(++index, stats->stamina);
    bstmt->SetData<uint32>(++index, stats->intellect);
    bstmt->SetData<uint32>(++index, stats->spirit);
    bstmt->SetData<uint32>(++index, stats->armor);
    bstmt->SetData<uint32>(++index, stats->defense);
    bstmt->SetData<uint32>(++index, stats->resHoly);
    bstmt->SetData<uint32>(++index, stats->resFire);
    bstmt->SetData<uint32>(++index, stats->resNature);
    bstmt->SetData<uint32>(++index, stats->resFrost);
    bstmt->SetData<uint32>(++index, stats->resShadow);
    bstmt->SetData<uint32>(++index, stats->resArcane);
    bstmt->setFloat (++index, stats->blockPct);
    bstmt->setFloat (++index, stats->dodgePct);
    bstmt->setFloat (++index, stats->parryPct);
    bstmt->setFloat (++index, stats->critPct);
    bstmt->SetData<uint32>(++index, stats->attackPower);
    bstmt->SetData<uint32>(++index, stats->spellPower);
    bstmt->SetData<uint32>(++index, stats->spellPen);
    bstmt->setFloat (++index, stats->hastePct);
    bstmt->setFloat (++index, stats->hitBonusPct);
    bstmt->SetData<uint32>(++index, stats->expertise);
    bstmt->setFloat (++index, stats->armorPenPct);

    CharacterDatabase.Execute(bstmt);
}

NpcBotAppearanceData const* BotDataMgr::SelectNpcBotAppearance(uint32 entry)
{
    NpcBotAppearanceDataMap::const_iterator itr = _botsAppearanceData.find(entry);
    return itr != _botsAppearanceData.end() ? itr->second : nullptr;
}

NpcBotExtras const* BotDataMgr::SelectNpcBotExtras(uint32 entry)
{
    NpcBotExtrasMap::const_iterator itr = _botsExtras.find(entry);
    return itr != _botsExtras.end() ? itr->second : nullptr;
}

void BotDataMgr::RegisterBot(Creature const* bot)
{
    if (_existingBots.find(bot) != _existingBots.end())
    {
        LOG_ERROR("entities.unit", "BotDataMgr::RegisterBot: bot %u (%s) already registered!",
            bot->GetEntry(), bot->GetName().c_str());
        return;
    }

    std::unique_lock<std::shared_mutex> lock(*GetLock());

    _existingBots.insert(bot);
    //LOG_ERROR("entities.unit", "BotDataMgr::RegisterBot: registered bot %u (%s)", bot->GetEntry(), bot->GetName().c_str());
}
void BotDataMgr::UnregisterBot(Creature const* bot)
{
    if (_existingBots.find(bot) == _existingBots.end())
    {
        LOG_ERROR("entities.unit", "BotDataMgr::UnregisterBot: bot %u (%s) not found!",
            bot->GetEntry(), bot->GetName().c_str());
        return;
    }

    std::unique_lock<std::shared_mutex> lock(*GetLock());

    _existingBots.erase(bot);
    //LOG_ERROR("entities.unit", "BotDataMgr::UnregisterBot: unregistered bot %u (%s)", bot->GetEntry(), bot->GetName().c_str());
}
Creature const* BotDataMgr::FindBot(uint32 entry)
{
    std::shared_lock<std::shared_mutex> lock(*GetLock());

    for (NpcBotRegistry::const_iterator ci = _existingBots.begin(); ci != _existingBots.end(); ++ci)
    {
        if ((*ci)->GetEntry() == entry)
            return *ci;
    }
    return nullptr;
}

NpcBotRegistry const& BotDataMgr::GetExistingNPCBots()
{
    return _existingBots;
}
