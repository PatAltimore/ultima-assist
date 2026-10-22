/*
 * assist.cpp -- ultima-assist: read-only views of the game state for the web
 * sidebar (web/shell.html), plus a few assist actions.  Everything here is
 * called from JavaScript between frames, so it only touches the savegame
 * and location data the engine already keeps up to date.
 */

#ifdef __EMSCRIPTEN__

#include <string.h>
#include <emscripten.h>
#include "context.h"
#include "map.h"
#include "savegame.h"
#include "tile.h"
#include "tileset.h"
#include "xu4.h"

#define EXPORT extern "C" EMSCRIPTEN_KEEPALIVE

static inline SaveGame* liveSave()
{
    if (xu4.stage != StagePlay || ! xu4.game || ! c)
        return NULL;
    return c->saveGame;
}

static inline const SaveGamePlayerRecord* member(int i)
{
    SaveGame* sg = liveSave();
    if (! sg || i < 0 || i >= int(sg->members) || i >= 8)
        return NULL;
    return sg->players + i;
}

// 0 = title/intro, 1 = playing.
EXPORT int assist_get_stage(void) { return liveSave() ? 1 : 0; }

// --- Party ---------------------------------------------------------------

EXPORT int assist_party_size(void)
{
    SaveGame* sg = liveSave();
    return sg ? int(sg->members) : 0;
}

EXPORT const char* assist_member_name(int i)
{
    const SaveGamePlayerRecord* p = member(i);
    return p ? p->name : "";
}

#define MEMBER_FIELD(NAME, FIELD) \
    EXPORT int assist_member_##NAME(int i) { \
        const SaveGamePlayerRecord* p = member(i); \
        return p ? int(p->FIELD) : 0; }

MEMBER_FIELD(hp,     hp)
MEMBER_FIELD(hp_max, hpMax)
MEMBER_FIELD(mp,     mp)
MEMBER_FIELD(xp,     xp)
MEMBER_FIELD(str,    str)
MEMBER_FIELD(dex,    dex)
MEMBER_FIELD(intel,  intel)
MEMBER_FIELD(weapon, weapon)
MEMBER_FIELD(armor,  armor)
MEMBER_FIELD(class,  klass)
MEMBER_FIELD(status, status)    // 'G'ood, 'P'oisoned, 'S'leeping, 'D'ead

// --- Resources -----------------------------------------------------------

#define SAVE_FIELD(NAME, FIELD) \
    EXPORT int assist_get_##NAME(void) { \
        SaveGame* sg = liveSave(); \
        return sg ? int(sg->FIELD) : 0; }

SAVE_FIELD(food,     food)      // 100 per displayed unit
SAVE_FIELD(gold,     gold)
SAVE_FIELD(torches,  torches)
SAVE_FIELD(gems,     gems)
SAVE_FIELD(keys,     keys)
SAVE_FIELD(sextants, sextants)
SAVE_FIELD(moves,    moves)
SAVE_FIELD(items,    items)     // Item bit flags
SAVE_FIELD(stones,   stones)    // Stone bit flags
SAVE_FIELD(runes,    runes)     // Rune bit flags
SAVE_FIELD(transport,transport)
SAVE_FIELD(ship_hull,shiphull)

EXPORT int assist_get_reagent(int i)
{
    SaveGame* sg = liveSave();
    return (sg && i >= 0 && i < REAG_MAX) ? sg->reagents[i] : 0;
}

// Virtue level 1..99; 0 means Avatarhood achieved in that virtue.
EXPORT int assist_get_virtue(int i)
{
    SaveGame* sg = liveSave();
    return (sg && i >= 0 && i < VIRT_MAX) ? sg->karma[i] : 0;
}

// --- Location and world --------------------------------------------------

EXPORT int assist_loc_valid(void)
{
    return (liveSave() && c->location && c->location->map) ? 1 : 0;
}

EXPORT const char* assist_loc_name(void)
{
    if (! assist_loc_valid())
        return "";
    const char* n = c->location->map->getName();
    return n ? n : "";
}

// Map::Type: 0 world, 1 city, 2 shrine, 3 combat, 4 dungeon.
EXPORT int assist_loc_type(void)
{
    return assist_loc_valid() ? int(c->location->map->type) : -1;
}

// Map id from the module's maps.b (0 = overworld; see web/guide-data.js).
EXPORT int assist_loc_map_id(void)
{
    return assist_loc_valid() ? int(c->location->map->id) : -1;
}

// Terrain class of an overworld tile, for the automap.  Only valid while the
// party is on the overworld; returns -1 otherwise.
//   0 other  1 water  2 swamp  3 grass  4 brush  5 forest  6 hills
//   7 mountains  8 place (town, castle, dungeon, shrine...)  9 bridge
EXPORT int assist_world_tile(int x, int y)
{
    if (! assist_loc_valid() || c->location->map->type != Map::WORLD)
        return -1;
    const Map* map = c->location->map;
    if (x < 0 || y < 0 || x >= int(map->width) || y >= int(map->height))
        return -1;
    const Tile* t = Tileset::findTileById(map->getTileFromData(Coords(x, y, 0)));
    const char* n = t ? t->nameStr() : NULL;
    if (! n)
        return 0;
    if (strstr(n, "bridge"))    return 9;
    if (strstr(n, "water") || strcmp(n, "sea") == 0 || strstr(n, "shallows"))
        return 1;
    if (strstr(n, "swamp"))     return 2;
    if (strstr(n, "grass"))     return 3;
    if (strstr(n, "brush"))     return 4;
    if (strstr(n, "forest"))    return 5;
    if (strstr(n, "hill"))      return 6;
    if (strstr(n, "mountain"))  return 7;
    if (strstr(n, "town") || strstr(n, "castle") || strstr(n, "village") || strstr(n, "city") ||
        strstr(n, "ruins") || strstr(n, "dungeon") || strstr(n, "shrine") ||
        strstr(n, "lcb") || strstr(n, "moongate"))
        return 8;
    return 0;
}

EXPORT int assist_loc_x(void) { return assist_loc_valid() ? c->location->coords.x : 0; }
EXPORT int assist_loc_y(void) { return assist_loc_valid() ? c->location->coords.y : 0; }
EXPORT int assist_loc_z(void) { return assist_loc_valid() ? c->location->coords.z : 0; }

EXPORT int assist_wind(void)     { return liveSave() ? c->windDirection : 0; }
EXPORT int assist_trammel(void)  { SaveGame* sg = liveSave(); return sg ? int(sg->trammelphase) : 0; }
EXPORT int assist_felucca(void)  { SaveGame* sg = liveSave(); return sg ? int(sg->feluccaphase) : 0; }

// --- Assist actions -------------------------------------------------------
// These edit the savegame in place; the status area catches up on the next
// turn (or the next time the game redraws it).

EXPORT void assist_heal_party(void)
{
    SaveGame* sg = liveSave();
    if (! sg)
        return;
    for (int i = 0; i < int(sg->members) && i < 8; ++i) {
        SaveGamePlayerRecord* p = sg->players + i;
        if (p->status == STAT_DEAD)
            continue;
        p->hp = p->hpMax;
        p->status = STAT_GOOD;
    }
}

EXPORT void assist_add_food(int units)
{
    SaveGame* sg = liveSave();
    if (! sg)
        return;
    int32_t f = sg->food + units * 100;
    sg->food = (f > 999900) ? 999900 : f;
}

EXPORT void assist_add_gold(int amount)
{
    SaveGame* sg = liveSave();
    if (! sg)
        return;
    int g = sg->gold + amount;
    sg->gold = (g > 9999) ? 9999 : g;
}

#endif  // __EMSCRIPTEN__
