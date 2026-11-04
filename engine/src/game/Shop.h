// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/Editing.h"

#include <string>
#include <vector>

namespace osf {

// BUY and SELL.
// Items are catalogue records (assets/item_catalog.tsv order).
enum ShopCategory { kShopMachinery, kShopSeeds, kShopLivestock, kShopBuildings, kShopSupplies };

std::vector<int> ShopItems(int category);                 // catalogue records offered in a BUY category
int ShopOwnRecord(int rec);
int ShopPrice(const GameState& s, int record);
int ShopPicture(const GameState& s, int record);          // record of alltiles.btl
int ShopTextId(const GameState& s, int record);           // record of itemtext.dat
int ShopMapTile(int record);                              // tile shown in the preview box (plain or masked id)
bool ShopNeedsPlacing(int record);                        // false: bought at once (seed)
// the cells a placed item covers from its top-left cell (the BUY pointer frame, user finding F28)
void ShopFootprint(const GameState& s, int record, int& w, int& h);
std::string ShopName(const GameState& s, int record);

// Buys one item; placed items go to cell (x, y).
EditResult ShopBuy(GameState& s, Simulation& sim, int record, int x = -1, int y = -1);

// What the player can sell: a list of (kind, index) with a label and a value.
struct SaleItem
{
    int kind;      // 0 crop stack (item record), 1 machine, 2 animal
    int index;
    std::string label;
    int64_t value;
    int picture;
    int quality = 0;   // 0..3, 3 best: the SELL row's rating badge (animals condition / 64, machines 3 - wear / 64,
                       // crop stacks 3 - age / 16384; as the original's value routines)
    int record = -1;   // catalogue record of the thing (its map picture in the SELL row)
};
// first tile of each tile family (as the original's table)
int FamilyStart(int family);
std::vector<SaleItem> SaleList(GameState& s, Simulation& sim, int kind);
EditResult ShopSell(GameState& s, Simulation& sim, const SaleItem& item);

// The Move (hand) tool: pick an object (1 machine, 2 animal, 3 barrel), then drop it on a free cell.
// the Move tool: what was picked at a cell, then dropped at another (fromX/fromY = the pick cell)
enum MoveKind { kMoveNone = 0, kMoveMachine = 1, kMoveAnimal = 2, kMoveBarrel = 3, kMoveItem = 4, kMoveTrough = 5, kMoveFeed = 6 };
bool PickObject(GameState& s, Simulation& sim, int x, int y, int& kind, int& index, std::string& why);
EditResult DropObject(GameState& s, Simulation& sim, int kind, int index, int fromX, int fromY, int x, int y);
int ItemAt(const GameState& s, int x, int y);

// What the Examine tool's card shows for a cell that is not a field.
struct ExamineCard
{
    enum Kind { kText, kLine, kStructure, kMachine, kAnimal, kItem };
    Kind kind = kText;
    int picture = 0;          // alltiles.btl record
    int textId = -1;          // itemtext.dat record: the whole record (kText) or its name (the others)
    std::string text;         // a fixed text when textId < 0 (kText wrapped, kLine one line)
    int damage = 0;           // kStructure: condition byte (+6)
    bool slots = false;       // kStructure: the storage slot grid is shown
    int cols = 0, rows = 0;   // slot grid, or the block of map tiles of a machine / animal / item
    std::vector<int> tiles;   // slot tiles (column c, row r at c * rows + r)
    int firstTile = 0;        // first map tile of the block (row by row)
    int quality = 0;          // 0..3 rating badge
    int value = 0;
};
bool ExamineCardAt(const GameState& s, int x, int y, ExamineCard& card);
// The town building a map tile belongs to: 1..9 for the 3 x 3 blocks of the 40-wide tile
// sheet that start at 320, 323, 326, 329, 332, 335, 338, 352 and 355; 0 for any other tile. Pressing on blocks 1-5
// opens Market Value, Sell, Bank, Buy and Farm Expert.
int TownBuildingAt(int tileId);

} // namespace osf
