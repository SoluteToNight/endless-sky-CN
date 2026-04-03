/* OutfitterPanel.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "OutfitterPanel.h"

#include "Color.h"
#include "DialogPanel.h"
#include "GameData.h"
#include "Hardpoint.h"
#include "Mission.h"
#include "Outfit.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Point.h"
#include "Rectangle.h"
#include "Screen.h"
#include "Ship.h"
#include "UI.h"
#include "Weapon.h"
#include "comparators/BySeriesAndIndex.h"
#include "image/Sprite.h"
#include "image/SpriteSet.h"
#include "shader/FillShader.h"
#include "shader/SpriteShader.h"
#include "text/Alignment.h"
#include "text/DisplayText.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "text/Truncate.h"


#include <algorithm>
#include <limits>
#include <memory>

using namespace std;

namespace {
// Label for the description field of the detail pane.
const string DESCRIPTION = "description";
const string LICENSE = " License";

// Numeric parameters for the visibility checkboxes. Note the checkboxes already
// have some padding of their own.
const Point checkboxSize{20, 20};
const Point keyPad{5, 8};

// Determine the refillable ammunition a particular ship consumes or stores.
set<const Outfit *> GetRefillableAmmunition(const Ship &ship) noexcept {
  auto toRefill = set<const Outfit *>{};
  for (auto &&it : ship.Weapons()) {
    const Weapon *weapon = it.GetWeapon();
    if (weapon && weapon->Ammo() && weapon->AmmoUsage() > 0)
      toRefill.emplace(weapon->Ammo());
  }

  // Carriers may be configured to supply ammunition for carried ships found
  // within the fleet. Since a particular ammunition outfit is not bound to
  // any particular weapon (i.e. one weapon may consume it, while another may
  // only require it be installed), we always want to restock these outfits.
  for (auto &&it : ship.Outfits()) {
    const Outfit *outfit = it.first;
    if (!outfit->GetWeapon() && outfit->AmmoStored())
      toRefill.emplace(outfit->AmmoStored());
  }
  return toRefill;
}

bool IsLicense(const string &name) { return name.ends_with(LICENSE); }

string LicenseRoot(const string &name) {
  return name.substr(0, name.length() - LICENSE.length());
}

string LocationName(OutfitterPanel::OutfitLocation location) {
  switch (location) {
  case OutfitterPanel::OutfitLocation::Ship:
    return "飞船";
  case OutfitterPanel::OutfitLocation::Shop:
    return "商店";
  case OutfitterPanel::OutfitLocation::Cargo:
    return "货舱";
  case OutfitterPanel::OutfitLocation::Storage:
    return "仓储";
  default:
    throw runtime_error("unreachable");
  }
}
} // namespace

OutfitterPanel::OutfitterPanel(PlayerInfo &player, Sale<Outfit> stock)
    : ShopPanel(player, true), outfitter(stock) {
  for (const pair<const string, Outfit> &it : GameData::Outfits())
    catalog[it.second.Category()].push_back(it.first);

  for (pair<const string, vector<string>> &it : catalog)
    sort(it.second.begin(), it.second.end(), BySeriesAndIndex<Outfit>());

  for (auto &ship : player.Ships())
    if (ship->GetPlanet() == planet)
      ++shipsHere;
}

void OutfitterPanel::Step() {
  CheckRefill();
  ShopPanel::Step();
  ShopPanel::CheckForMissions(Mission::OUTFITTER);
  ShopPanel::ValidateSelectedShips();
  if (GetUI().IsTop(this) && !checkedHelp)
    // Use short-circuiting to only display one of them at a time.
    // (The first valid condition encountered will make us skip the others.)
    if (DoHelp("outfitter") || DoHelp("cargo management") ||
        DoHelp("uninstalling and storage") ||
        (shipsHere > 1 && DoHelp("outfitter with multiple ships")) || true)
      // Either a help message was freshly displayed, or all of them have
      // already been seen.
      checkedHelp = true;
}

int OutfitterPanel::TileSize() const { return OUTFIT_SIZE; }

int OutfitterPanel::VisibilityCheckboxesSize() const {
  return 4 * checkboxSize.Y() + 2. * keyPad.Y();
}

bool OutfitterPanel::HasItem(const string &name) const {
  const Outfit *outfit = GameData::Outfits().Get(name);
  if (showForSale && (outfitter.Has(outfit) || player.Stock(outfit) > 0))
    return true;

  if (showCargo && player.Cargo().Get(outfit))
    return true;

  if (showStorage && player.Storage().Get(outfit))
    return true;

  if (showInstalled)
    for (const Ship *ship : playerShips)
      if (ship->OutfitCount(outfit))
        return true;

  if (showForSale && HasLicense(name))
    return true;

  return false;
}

void OutfitterPanel::DrawItem(const string &name, const Point &point) {
  const Outfit *outfit = GameData::Outfits().Get(name);
  zones.emplace_back(point, Point(OUTFIT_SIZE, OUTFIT_SIZE), outfit);
  if (point.Y() + OUTFIT_SIZE / 2 < Screen::Top() ||
      point.Y() - OUTFIT_SIZE / 2 > Screen::Bottom())
    return;

  bool isSelected = (outfit == selectedOutfit);
  bool isOwned = playerShip && playerShip->OutfitCount(outfit);
  DrawOutfit(*outfit, point, isSelected, isOwned);

  // Check if this outfit is a "license".
  bool isLicense = IsLicense(name);
  int mapSize = outfit->Get("map");

  const Font &font = FontSet::Get(14);
  const Color &bright = *GameData::Colors().Get("bright");
  const Color &highlight =
      *GameData::Colors().Get("outfitter difference highlight");

  bool highlightDifferences = false;
  if (playerShip || isLicense || mapSize) {
    int minCount = numeric_limits<int>::max();
    int maxCount = 0;
    if (isLicense)
      minCount = maxCount = player.HasLicense(LicenseRoot(name));
    else if (mapSize) {
      bool mapMinables = outfit->Get("map minables");
      minCount = maxCount = player.HasMapped(mapSize, mapMinables);
    } else {
      highlightDifferences = true;
      string firstModelName;
      for (const Ship *ship : playerShips) {
        // Highlight differences in installed outfit counts only when all
        // selected ships are of the same model.
        string modelName = ship->TrueModelName();
        if (firstModelName.empty())
          firstModelName = modelName;
        else
          highlightDifferences &= (modelName == firstModelName);
        int count = ship->OutfitCount(outfit);
        minCount = min(minCount, count);
        maxCount = max(maxCount, count);
      }
    }

    if (maxCount) {
      string label = "已安装：" + to_string(minCount);
      Color color = bright;
      if (maxCount > minCount) {
        label += " - " + to_string(maxCount);
        if (highlightDifferences)
          color = highlight;
      }

      Point labelPos =
          point + Point(-OUTFIT_SIZE / 2 + 20, OUTFIT_SIZE / 2 - 38);
      font.Draw(label, labelPos, color);
    }
  }

  // Don't show the "in stock" amount if the outfit has an unlimited stock.
  int stock = 0;
  if (!outfitter.Has(outfit))
    stock = max(0, player.Stock(outfit));
  int cargo = player.Cargo().Get(outfit);
  int storage = player.Storage().Get(outfit);

  string message;
  if (cargo && storage && stock)
    message = "货舱+仓储：" + to_string(cargo + storage) +
              "，库存：" + to_string(stock);
  else if (cargo && storage)
    message =
        "货舱：" + to_string(cargo) + "，仓储：" + to_string(storage);
  else if (cargo && stock)
    message =
        "货舱：" + to_string(cargo) + "，库存：" + to_string(stock);
  else if (storage && stock)
    message =
        "仓储：" + to_string(storage) + "，库存：" + to_string(stock);
  else if (cargo)
    message = "货舱：" + to_string(cargo);
  else if (storage)
    message = "仓储：" + to_string(storage);
  else if (stock)
    message = "库存：" + to_string(stock);
  else if (!outfitter.Has(outfit))
    message = "（此地不售）";
  if (!message.empty()) {
    Point pos = point + Point(OUTFIT_SIZE / 2 - 20 - font.Width(message),
                              OUTFIT_SIZE / 2 - 24);
    font.Draw(message, pos, bright);
  }
}

double OutfitterPanel::DrawDetails(const Point &center) {
  string selectedItem = "未选择任何装备";
  const Font &font = FontSet::Get(14);

  double heightOffset = 20.;

  if (selectedOutfit) {
    outfitInfo.Update(
        *selectedOutfit, player,
        CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Shop) ||
            CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Shop) ||
            CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Shop),
        collapsed.contains(DESCRIPTION));
    selectedItem = selectedOutfit->DisplayName();

    const Sprite *thumbnail = selectedOutfit->Thumbnail();
    const float tileSize =
        thumbnail ? max(thumbnail->Height(), static_cast<float>(TileSize()))
                  : static_cast<float>(TileSize());
    const Point thumbnailCenter(center.X(), center.Y() + 20 +
                                                static_cast<int>(tileSize / 2));
    const Point startPoint(center.X() - INFOBAR_WIDTH / 2 + 20,
                           center.Y() + 20 + tileSize);

    const Sprite *background = SpriteSet::Get("ui/outfitter unselected");
    SpriteShader::Draw(background, thumbnailCenter);
    if (thumbnail) {
      if (thumbnail->IsLoaded())
        SpriteShader::Draw(thumbnail, thumbnailCenter);
      else
        loadingCircle.Draw(thumbnailCenter);
    }

    const bool hasDescription = outfitInfo.DescriptionHeight();

    double descriptionOffset = hasDescription ? 40. : 0.;

    if (hasDescription) {
      if (!collapsed.contains(DESCRIPTION)) {
        descriptionOffset = outfitInfo.DescriptionHeight();
        outfitInfo.DrawDescription(startPoint);
      } else {
        const Color &dim = *GameData::Colors().Get("medium");
        font.Draw(DESCRIPTION, startPoint + Point(35., 12.), dim);
        const Sprite *collapsedArrow = SpriteSet::Get("ui/collapsed");
        SpriteShader::Draw(collapsedArrow, startPoint + Point(20., 20.));
      }

      // Calculate the ClickZone for the description and add it.
      const Point descriptionDimensions(INFOBAR_WIDTH, descriptionOffset);
      const Point descriptionCenter(center.X(),
                                    startPoint.Y() + descriptionOffset / 2);
      ClickZone<string> collapseDescription = ClickZone<string>(
          descriptionCenter, descriptionDimensions, DESCRIPTION);
      categoryZones.emplace_back(collapseDescription);
    }

    const Point requirementsPoint(startPoint.X(),
                                  startPoint.Y() + descriptionOffset);
    const Point attributesPoint(startPoint.X(),
                                requirementsPoint.Y() +
                                    outfitInfo.RequirementsHeight());
    outfitInfo.DrawRequirements(requirementsPoint);
    outfitInfo.DrawAttributes(attributesPoint);

    heightOffset = attributesPoint.Y() + outfitInfo.AttributesHeight();
  }

  // Draw this string representing the selected item (if any), centered in the
  // details side panel.
  const Color &bright = *GameData::Colors().Get("bright");
  Point selectedPoint(center.X() - INFOBAR_WIDTH / 2, center.Y());
  font.Draw(
      {selectedItem, {INFOBAR_WIDTH, Alignment::CENTER, Truncate::MIDDLE}},
      selectedPoint, bright);

  return heightOffset;
}

ShopPanel::TransactionResult
OutfitterPanel::CanMoveOutfit(OutfitLocation fromLocation,
                              OutfitLocation toLocation,
                              const string &actionName) const {
  if (!planet || !selectedOutfit)
    return "未选择装备。";

  // Prevent coding up bad combinations.
  if (fromLocation == toLocation)
    throw runtime_error("unreachable; to and from are the same");
  if (fromLocation == OutfitLocation::Shop &&
      toLocation == OutfitLocation::Storage)
    throw runtime_error("unreachable; unsupported to/from combination");

  // Handle special cases such as maps and licenses.
  int mapSize = selectedOutfit->Get("map");
  if (mapSize) {
    if (fromLocation != OutfitLocation::Shop)
      return "你无法" + actionName + "地图。购买后将永久生效。";
    if (toLocation == OutfitLocation::Cargo ||
        toLocation == OutfitLocation::Storage)
      return "你无法将地图放入" + LocationName(toLocation) + "。";
    bool mapMinables = selectedOutfit->Get("map minables");
    if (mapSize > 0 && player.HasMapped(mapSize, mapMinables))
      return "你已探索该地图显示的全部星系，无需重复购买。";
    return true;
  }

  if (IsLicense(selectedOutfit->TrueName())) {
    if (HasLicense(selectedOutfit->TrueName())) {
      if (fromLocation != OutfitLocation::Shop)
        return "你无法" + actionName + "执照。获得后将永久生效。";
      if (toLocation == OutfitLocation::Cargo ||
          toLocation == OutfitLocation::Storage)
        return "你无法将执照放入" + LocationName(toLocation) + "。";
      return "你已拥有该执照，无需重复购买。";
    }
    return true;
  }

  bool canSource = false;

  // Handle reasons why the outfit may not be moved from fromLocation.
  switch (fromLocation) {
  case OutfitLocation::Ship: {
    if (!playerShip)
      return "未选择飞船。";

    vector<pair<string, vector<string>>> dependentOutfitErrors;
    vector<string> errorDetails;
    bool foundOutfit = false;
    for (const Ship *ship : playerShips) {
      if (ship->OutfitCount(selectedOutfit) > 0) {
        foundOutfit = true;
        Outfit attributes = ship->Attributes();
        // If this outfit requires ammo, check if we could sell it if we sold
        // all the ammo for it first.
        const Outfit *ammo = selectedOutfit->AmmoStoredOrUsed();
        if (ammo && ship->OutfitCount(ammo)) {
          attributes.Add(*ammo, -ship->OutfitCount(ammo));
        }
        // Ammo is not a factor (now), check whether this ship can uninstall
        // this outfit.
        canSource = attributes.CanAdd(*selectedOutfit, -1);

        // If we have an outfit that can be sourced, break out, otherwise return
        // an appropriate error.
        if (canSource)
          break;

        // At least one of the outfit is installed on at least one of the
        // selected ships. Create a complete summary of reasons why none of this
        // outfit were able to be <verb>'d: Looping over each ship which has the
        // selected outfit, identify the reasons why it cannot be <verb>'d. Make
        // a list of ship to errors to assemble into a string afterward.
        // TODO: when there are multiples of the same better verbiage could be
        // used rather than restating.
        for (const pair<const char *, double> &it :
             selectedOutfit->Attributes())
          if (attributes.Get(it.first) < it.second) {
            for (const auto &sit : ship->Outfits())
              if (sit.first->Get(it.first) < 0.)
                errorDetails.emplace_back(
                    string("the \"") + sit.first->DisplayName() +
                    "\" "
                    "依赖此装备，必须先将其卸载");
            if (errorDetails.empty())
              errorDetails.emplace_back(
                  string("\"") + it.first +
                  "\" value would be reduced to less than zero");
          }

        if (!errorDetails.empty())
          dependentOutfitErrors.emplace_back(ship->GivenName(),
                                             std::move(errorDetails));
      }
    }

    if (canSource)
      break;

    // The outfit cannot be installed from any ship.
    if (!foundOutfit)
      return "你没有可用于" + actionName + "的" + selectedOutfit->PluralName() + "。";

    // Return the errors in the appropriate format.
    if (!dependentOutfitErrors.empty()) {
      string errorMessage =
          "你无法" + actionName +
          (playerShips.size() > 1 ? "任一已选飞船上的该装备" : "你飞船上的该装备") +
          "，原因如下：\n";
      int i = 0;
      for (const auto &[shipName, errors] : dependentOutfitErrors) {
        if (playerShips.size() > 1) {
          errorMessage += to_string(++i) + ". 无法从\"" + shipName + "\"" +
                          actionName + "该装备，原因如下：\n";
        }
        for (const string &error : errors)
          errorMessage += "- " + error + '\n';
      }
      return errorMessage;
    }

    break;
  }
  case OutfitLocation::Shop: {
    // If outfit is not available in the Outfitter, respond that it can't be
    // bought here.
    if (!(outfitter.Has(selectedOutfit) || player.Stock(selectedOutfit) > 0)) {
      return "你无法在此处购买该装备。它之所以显示在列表中，是因为你已拥有该装备，"
             "但这个" + planet->Noun() + "并不出售它。";
    }

    // Check special unique outfits, if you already have them.
    // Skip this for speed if this action is not a buy and install action.
    if (toLocation != OutfitLocation::Ship) {
      bool mapMinables = selectedOutfit->Get("map minables");
      if (mapSize > 0 && player.HasMapped(mapSize, mapMinables))
        return "你已探索该地图显示的全部星系，无需重复购买。";

      if (HasLicense(selectedOutfit->TrueName()))
        return "你已拥有该执照，无需重复购买。";
    }

    // Determine what you will have to pay to buy this outfit.
    int64_t cost = player.StockDepreciation().Value(selectedOutfit, day);
    int64_t credits = player.Accounts().Credits();
    if (cost > credits)
      return "你的资金不足以购买该装备。还需要" +
             Format::CreditString(cost - credits);

    // Add the cost to buy the required license.
    int64_t licenseCost = LicenseCost(selectedOutfit, false);
    if (cost + licenseCost > credits)
      return "你的资金不足以购买该装备，因为还需要购买其执照。还需要" +
             Format::CreditString(cost + licenseCost - credits);

    // Check that the player has any necessary licenses.
    if (licenseCost < 0)
      return "你无法购买该装备，因为缺少所需执照。";

    // The outfit can be purchased (available in the outfitter, licensed and
    // affordable).
    canSource = true;
    break;
  }
  case OutfitLocation::Cargo: {
    // Do we have any in cargo?
    if (!player.Cargo().Get(selectedOutfit))
      return "你的货舱中没有可用于" + actionName + "的" + selectedOutfit->PluralName() + "。";
    canSource = true;
    break;
  }
  case OutfitLocation::Storage: {
    // Do we have any in storage?
    if (!player.Storage().Get(selectedOutfit))
      return "你的仓储中没有可用于" + actionName + "的" + selectedOutfit->PluralName() + "。";
    canSource = true;
    break;
  }
  default:
    throw runtime_error("unreachable");
  }

  // Collect relevant errors.
  vector<string> errors;
  bool canPlace = false;

  // Handle reasons why the outfit may not be moved to toLocation.
  switch (toLocation) {
  case OutfitLocation::Ship: {
    if (!playerShip)
      return "未选择飞船。";

    // Find if any ship can install the outfit.
    for (const Ship *ship : playerShips)
      if (ShipCanAdd(ship, selectedOutfit)) {
        canPlace = true;
        break;
      }

    if (!canPlace) {
      // If no selected ship can install the outfit, report error based on
      // playerShip.
      double outfitNeeded = -selectedOutfit->Get("outfit space");
      double outfitSpace = playerShip->Attributes().Get("outfit space");
      if (outfitNeeded > outfitSpace)
        errors.push_back(
            "无法安装该装备：它占用" +
            Format::CargoString(outfitNeeded, "装备空间") +
            "，而这艘船仅有" + Format::MassString(outfitSpace) +
            "可用。");

      double weaponNeeded = -selectedOutfit->Get("weapon capacity");
      double weaponSpace = playerShip->Attributes().Get("weapon capacity");
      if (weaponNeeded > weaponSpace)
        errors.push_back(
            "你的飞船只有部分装备容量可用于武器。无法安装该装备：它占用" +
            Format::CargoString(weaponNeeded, "武器空间") +
            "，而这艘船仅有" + Format::MassString(weaponSpace) +
            "可用。");

      double engineNeeded = -selectedOutfit->Get("engine capacity");
      double engineSpace = playerShip->Attributes().Get("engine capacity");
      if (engineNeeded > engineSpace)
        errors.push_back(
            "你的飞船只有部分装备容量可用于引擎。无法安装该装备：它占用" +
            Format::CargoString(engineNeeded, "引擎空间") +
            "，而这艘船仅有" + Format::MassString(engineSpace) +
            "可用。");

      if (selectedOutfit->Category() == "Ammunition")
        errors.emplace_back(
            !playerShip->OutfitCount(selectedOutfit)
                ? "该装备是某种武器的弹药。"
                  "未先安装对应武器时无法安装该弹药。"
                : "该武器的弹药已达上限。若要安装更多弹药，请先再安装一件该武器。");

      int mountsNeeded = -selectedOutfit->Get("turret mounts");
      int mountsFree = playerShip->Attributes().Get("turret mounts");
      if (mountsNeeded > mountsFree)
        errors.emplace_back(
            "该武器需要安装在炮塔挂点上，但你的飞船没有可用炮塔挂点。");

      int gunsNeeded = -selectedOutfit->Get("gun ports");
      int gunsFree = playerShip->Attributes().Get("gun ports");
      if (gunsNeeded > gunsFree)
        errors.emplace_back(
            "该武器需要安装在炮位上，但你的飞船没有可用炮位。");

      if (selectedOutfit->Get("installable") < 0.)
        errors.emplace_back(
            "该物品不是可安装到飞船上的装备。");

      // Handle other attributes more generically, if none of the above are the
      // problem.
      if (errors.empty())
        for (const pair<const char *, double> &it :
             selectedOutfit->Attributes()) {
          // If playerShip has fewer of this attribute available than required
          // by the selectedOutfit, add this attribute to the list of
          // deficiencies.
          double shipAvailable = playerShip->Attributes().Get(it.first);
          double outfitRequires = -it.second;
          if (shipAvailable < outfitRequires)
            errors.push_back(
                "无法安装该装备：需要 " +
                Format::SimplePluralization(outfitRequires,
                                            '\'' + string(it.first) + '\'') +
                "，而这艘船仅有 " + Format::Number(shipAvailable) +
                " 可用。");
        }

      // Return the errors in the appropriate format.
      if (errors.empty())
        canPlace = true;
      else if (errors.size() == 1)
        return errors[0];
      else {
        string errorMessage = "无法" + actionName + "该装备，原因如下：\n";
        for (const string &error : errors)
          errorMessage += "- " + error + '\n';
        return errorMessage;
      }
    }
    break;
  }
  case OutfitLocation::Shop: {
    // Can sell anything to the shop that can be removed from elsewhere.
    canPlace = true;
    break;
  }
  case OutfitLocation::Cargo: {
    // Check fleet cargo space vs outfit mass.
    double mass = selectedOutfit->Mass();
    double freeCargo = player.Cargo().FreePrecise();
    if (!mass || freeCargo >= mass)
      canPlace = true;
    else
      return "你无法将该装备装入货舱，因为它占用" +
             Format::CargoString(mass, "质量") + "，而你的舰队仅有" +
             Format::CargoString(freeCargo, "货舱空间") + "可用。";
    break;
  }
  case OutfitLocation::Storage: {
    // Can store anything in storage that can be removed from elsewhere.
    canPlace = true;
    break;
  }
  default:
    throw runtime_error("unreachable");
  }

  return canSource && canPlace;
}

ShopPanel::TransactionResult
OutfitterPanel::MoveOutfit(OutfitLocation fromLocation,
                           OutfitLocation toLocation,
                           const string &actionName) const {
  // Source up outfits from the <fromLocation> and move them to the specified
  // <toLocation>. If ships are the to/from location, then each ship will
  // install/uninstall up to <howManyPer> outfits each, as allowed. Otherwise,
  // it's simply how many per shop/hold when ships are not involved in the move.

  // Note: CanMoveOutfit must be checked prior to further execution.
  TransactionResult canMove =
      CanMoveOutfit(fromLocation, toLocation, actionName);
  if (!canMove)
    return canMove;

  // The count of how many outfits will be moved will be per ship when ships are
  // involved, otherwise simply per hold. Hence, the concept of how many "per"
  // rather than how many in total.
  int howManyPer = Modifier();

  // Purchases are handled here.
  if (fromLocation == OutfitLocation::Shop) {
    // Buy the required license.
    int64_t licenseCost = LicenseCost(selectedOutfit, false);
    if (licenseCost) {
      player.Accounts().AddCredits(-licenseCost);
      for (const string &licenseName : selectedOutfit->Licenses())
        if (!player.HasLicense(licenseName))
          player.AddLicense(licenseName);
    }

    // Special case: maps.
    int mapSize = selectedOutfit->Get("map");
    if (mapSize) {
      bool mapMinables = selectedOutfit->Get("map minables");
      player.Map(mapSize, mapMinables);
      player.Accounts().AddCredits(-selectedOutfit->Cost());
      return true;
    }

    // Special case: licenses.
    if (IsLicense(selectedOutfit->TrueName())) {
      player.AddLicense(LicenseRoot(selectedOutfit->TrueName()));
      player.Accounts().AddCredits(-selectedOutfit->Cost());
      return true;
    }

    // Buy up to <multiplier> outfits in total from the shop.
    int64_t cost = player.StockDepreciation().Value(selectedOutfit, day);

    if (toLocation == OutfitLocation::Ship) {
      // Buy and install on the selected ships.
      for (int i = howManyPer; i &&
                               (outfitter.Has(selectedOutfit) ||
                                player.Stock(selectedOutfit) > 0) &&
                               cost <= player.Accounts().Credits();
           --i) {
        // Find the ships with the fewest number of these outfits.
        const vector<Ship *> shipsToOutfit = GetShipsToOutfit(true);
        if (shipsToOutfit.empty())
          break;
        for (Ship *ship : shipsToOutfit) {
          if (!(outfitter.Has(selectedOutfit) ||
                player.Stock(selectedOutfit) > 0) ||
              cost > player.Accounts().Credits())
            // Out of stock or money.
            break;

          // Pay for it and remove it from available stock.
          player.Accounts().AddCredits(-cost);
          player.AddStock(selectedOutfit, -1);

          // Install it on this ship.
          ship->AddOutfit(selectedOutfit, 1);
          int required = selectedOutfit->Get("required crew");
          if (required && ship->Crew() + required <=
                              static_cast<int>(ship->Attributes().Get("bunks")))
            ship->AddCrew(required);
          ship->Recharge();
        }
      }
    } else if (toLocation == OutfitLocation::Cargo) {
      if (!outfitter.Has(selectedOutfit))
        howManyPer = min(howManyPer, player.Stock(selectedOutfit));
      // Buy up to <modifier> of the selected outfit and place them in fleet
      // cargo.
      double mass = selectedOutfit->Mass();
      if (mass)
        howManyPer = min(howManyPer,
                         static_cast<int>(player.Cargo().FreePrecise() / mass));

      // How much will it cost to buy all that we can fit?
      int64_t price =
          player.StockDepreciation().Value(selectedOutfit, day, howManyPer);

      // Adjust the number to buy to stay within the player's available credits.
      while (price > player.Accounts().Credits() && howManyPer) {
        // The current cost is more than the amount of money the player has, so
        // try a smaller amount.
        if (!--howManyPer)
          // If the smaller amount becomes zero, we're not able to do anything
          // with cargo.
          return "资金不足，无法购买并装入货舱。";
        price =
            player.StockDepreciation().Value(selectedOutfit, day, howManyPer);
      }

      if (howManyPer) {
        // Buy as many as can be paid for and will fit.
        player.Accounts().AddCredits(-price);
        player.AddStock(selectedOutfit, -howManyPer);

        // Put them into fleet cargo.
        player.Cargo().Add(selectedOutfit, howManyPer);
      }
    }
    // Note: Buying into storage not implemented. Why waste your money?
  } else if (fromLocation == OutfitLocation::Ship) {
    for (int i = howManyPer; i; --i) {
      // Get the ships that have the most of this outfit installed.
      const vector<Ship *> shipsToOutfit = GetShipsToOutfit();
      // Note: to get here, we have already confirmed that every ship in the
      // selection has the outfit installed and that the outfit can be
      // uninstalled in the first place.
      if (shipsToOutfit.empty())
        break;
      for (Ship *ship : shipsToOutfit) {
        // Uninstall the outfit.
        ship->AddOutfit(selectedOutfit, -1);
        int required = selectedOutfit->Get("required crew");
        if (required)
          ship->AddCrew(-required);
        // Adjust hired crew counts.
        ship->Recharge();

        if (toLocation == OutfitLocation::Shop) {
          // Do the sale.
          int64_t price = player.FleetDepreciation().Value(selectedOutfit, day);
          player.Accounts().AddCredits(price);
          player.AddStock(selectedOutfit, 1);
        }
        // If the context is uninstalling, move the outfit into Storage.
        else if (toLocation == OutfitLocation::Storage)
          player.Storage().Add(selectedOutfit, 1);
        // Note: It would be easy to add conditional statements above to also
        // support uninstall into cargo, this is not supported in the outfitter
        // at this time.

        // Move ammo to storage.
        // Since some outfits have ammo, remove any ammo that must also be moved
        // as there aren't enough supporting slots for said ammo once this
        // outfit is removed.
        const Outfit *ammo = selectedOutfit->AmmoStoredOrUsed();
        if (ammo && ship->OutfitCount(ammo)) {
          // Determine how many of this ammo we must uninstall to also uninstall
          // the launcher.
          int mustUninstall = 0;
          for (const pair<const char *, double> &it :
               ship->Attributes().Attributes())
            if (it.second < 0.)
              mustUninstall = max<int>(mustUninstall,
                                       ceil(it.second / ammo->Get(it.first)));

          if (mustUninstall) {
            ship->AddOutfit(ammo, -mustUninstall);

            if (toLocation == OutfitLocation::Shop) {
              // Do the sale of the outfit's ammo.
              int64_t price =
                  player.FleetDepreciation().Value(ammo, day, mustUninstall);
              player.Accounts().AddCredits(price);
              player.AddStock(ammo, mustUninstall);
            }
            // If the context is uninstalling, move the outfit's ammo into
            // Storage.
            else if (toLocation == OutfitLocation::Storage)
              player.Storage().Add(ammo, mustUninstall);
            // Note: It would be easy to add conditional statements above to
            // also support uninstall into cargo, this is not supported in the
            // outfitter at this time.
          }
        }
      }
    }
    // Note: Uninstalling into cargo could be implemented below, but not
    // supported in current outfitter logic.
  } else if (fromLocation == OutfitLocation::Storage ||
             fromLocation == OutfitLocation::Cargo) {
    CargoHold &hold = fromLocation == OutfitLocation::Cargo ? player.Cargo()
                                                            : player.Storage();
    CargoHold &otherHold = fromLocation == OutfitLocation::Cargo
                               ? player.Storage()
                               : player.Cargo();

    if (toLocation == OutfitLocation::Shop) {
      // Do the sale.
      howManyPer = hold.Remove(selectedOutfit, howManyPer);
      int64_t price =
          player.FleetDepreciation().Value(selectedOutfit, day, howManyPer);
      player.Accounts().AddCredits(price);
      player.AddStock(selectedOutfit, howManyPer);
    } else if (toLocation == OutfitLocation::Ship) {
      for (int i = howManyPer; i && hold.Get(selectedOutfit); --i) {
        // Find the ships with the fewest number of these outfits.
        const vector<Ship *> shipsToOutfit = GetShipsToOutfit(true);
        if (shipsToOutfit.empty())
          break;
        for (Ship *ship : shipsToOutfit) {
          // If there were no more outfits in cargo, bail out.
          if (!hold.Remove(selectedOutfit))
            break;

          // Install it on this ship.
          ship->AddOutfit(selectedOutfit, 1);
          int required = selectedOutfit->Get("required crew");
          if (required && ship->Crew() + required <=
                              static_cast<int>(ship->Attributes().Get("bunks")))
            ship->AddCrew(required);
          ship->Recharge();
        }
      }
    } else
      // Move up to <modifier> from storage to cargo or cargo to storage (hold
      // to otherHold).
      hold.Transfer(selectedOutfit, howManyPer, otherHold);
  }

  return canMove;
}

bool OutfitterPanel::ButtonActive(char key, bool shipRelatedOnly) {
  if (key == 'b')
    return static_cast<bool>(
        CanMoveOutfit(OutfitLocation::Shop, OutfitLocation::Ship));
  if (key == 'i')
    return CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Ship) ||
           CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Ship);
  if (key == 'c')
    return !shipRelatedOnly &&
           (CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Cargo) ||
            CanMoveOutfit(OutfitLocation::Shop, OutfitLocation::Cargo));
  if (key == 's')
    return (!shipRelatedOnly &&
            (CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Shop) ||
             CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Shop))) ||
           CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Shop);
  if (key == 'u' || key == 'r')
    return CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage) ||
           (!shipRelatedOnly &&
            CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Storage));
  return false;
}

bool OutfitterPanel::ShouldHighlight(const Ship *ship) {
  if (!selectedOutfit)
    return false;

  if (!ButtonActive(hoverButton, true))
    return false;

  // If we're hovering above a button that can add outfits to a ship then
  // highlight the ship.
  if (hoverButton == 'b' || hoverButton == 'i')
    return ShipCanAdd(ship, selectedOutfit);

  // Otherwise, not installing, highlight ships which can have outfits removed.
  return ShipCanRemove(ship, selectedOutfit);
}

// Draw the display filter selection checkboxes in the lower left of the outfit
// panel.
void OutfitterPanel::DrawKey() {
  const Sprite *back = SpriteSet::Get("ui/outfitter key");

  SpriteShader::Draw(back, Screen::BottomLeft() +
                               .5 * Point(back->Width(), -back->Height()));

  const Font &font = FontSet::Get(14);
  const Color color[2] = {*GameData::Colors().Get("medium"),
                          *GameData::Colors().Get("bright")};
  const Sprite *box[2] = {SpriteSet::Get("ui/unchecked"),
                          SpriteSet::Get("ui/checked")};

  Point pos =
      Screen::BottomLeft() +
      Point{keyPad.X() + .5 * checkboxSize.X(),
            -VisibilityCheckboxesSize() + .5 * checkboxSize.Y() + keyPad.Y()};
  const Point labelOffset{.5 * checkboxSize.X(), -.5 * font.Height()};
  const Point activeAreaSize{180., checkboxSize.Y()};
  // Include the label and the sprite (20px) in the active checkbox area.
  const Point checkboxOffset{.5 * activeAreaSize.X() - .5 * checkboxSize.X(),
                             0.};

  SpriteShader::Draw(box[showForSale], pos);
  font.Draw("显示在售装备", pos + labelOffset, color[showForSale]);
  AddZone(Rectangle(pos + checkboxOffset, activeAreaSize),
          [this]() { ToggleForSale(); });

  pos.Y() += checkboxSize.Y();
  SpriteShader::Draw(box[showInstalled], pos);
  // The text color will be "medium" when no ships are selected, regardless of
  // checkmark state, indicating that the selection is invalid (invalid
  // context).
  font.Draw("显示已安装装备", pos + labelOffset,
            color[showInstalled && playerShip]);
  AddZone(Rectangle(pos + checkboxOffset, activeAreaSize),
          [this]() { ToggleInstalled(); });

  pos.Y() += checkboxSize.Y();
  SpriteShader::Draw(box[showCargo], pos);
  font.Draw("显示货舱中的装备", pos + labelOffset, color[showCargo]);
  AddZone(Rectangle(pos + checkboxOffset, activeAreaSize),
          [this]() { ToggleCargo(); });

  pos.Y() += checkboxSize.Y();
  SpriteShader::Draw(box[showStorage], pos);
  font.Draw("显示仓储中的装备", pos + labelOffset, color[showStorage]);
  AddZone(Rectangle(pos + checkboxOffset, activeAreaSize),
          [this]() { ToggleStorage(); });
}

void OutfitterPanel::ToggleForSale() {
  showForSale = !showForSale;

  CheckSelection();
  delayedAutoScroll = true;
}

void OutfitterPanel::ToggleInstalled() {
  showInstalled = !showInstalled;

  CheckSelection();
  delayedAutoScroll = true;
}

void OutfitterPanel::ToggleStorage() {
  showStorage = !showStorage;

  CheckSelection();
  delayedAutoScroll = true;
}

void OutfitterPanel::ToggleCargo() {
  showCargo = !showCargo;

  CheckSelection();
  delayedAutoScroll = true;
}

// Returns true if this ship can install the selected outfit.
bool OutfitterPanel::ShipCanAdd(const Ship *ship, const Outfit *outfit) {
  return (ship->Attributes().CanAdd(*outfit, 1) > 0);
}

// Returns true if this ship can uninstall the selected outfit.
bool OutfitterPanel::ShipCanRemove(const Ship *ship, const Outfit *outfit) {
  if (!ship->OutfitCount(outfit))
    return false;

  // If this outfit requires ammo, check if we could sell it if we sold all
  // the ammo for it first.
  const Outfit *ammo = outfit->AmmoStoredOrUsed();
  if (ammo && ship->OutfitCount(ammo)) {
    Outfit attributes = ship->Attributes();
    attributes.Add(*ammo, -ship->OutfitCount(ammo));
    return attributes.CanAdd(*outfit, -1);
  }

  // Ammo is not a factor, so check whether this ship can uninstall this outfit
  // by itself.
  return ship->Attributes().CanAdd(*outfit, -1);
}

void OutfitterPanel::DrawOutfit(const Outfit &outfit, const Point &center,
                                bool isSelected, bool isOwned) const {
  const Sprite *thumbnail = outfit.Thumbnail();
  const Sprite *back = SpriteSet::Get(isSelected ? "ui/outfitter selected"
                                                 : "ui/outfitter unselected");
  SpriteShader::Draw(back, center);
  if (thumbnail) {
    if (thumbnail->IsLoaded())
      SpriteShader::Draw(thumbnail, center);
    else
      loadingCircle.Draw(center);
  }

  // Draw the outfit name.
  const string &name = outfit.DisplayName();
  const Font &font = FontSet::Get(14);
  Point offset(-.5 * OUTFIT_SIZE, -.5 * OUTFIT_SIZE + 10.);
  font.Draw({name, {OUTFIT_SIZE, Alignment::CENTER, Truncate::MIDDLE}},
            center + offset, Color((isSelected | isOwned) ? .8 : .5, 0.));
}

bool OutfitterPanel::HasLicense(const string &name) const {
  return (IsLicense(name) && player.HasLicense(LicenseRoot(name)));
}

void OutfitterPanel::CheckRefill() {
  if (checkedRefill)
    return;
  checkedRefill = true;

  int count = 0;
  map<const Outfit *, int> needed;
  for (const shared_ptr<Ship> &ship : player.Ships()) {
    // Skip ships in other systems and those that were unable to land in-system.
    if (ship->GetSystem() != player.GetSystem() || ship->IsDisabled())
      continue;

    ++count;
    auto toRefill = GetRefillableAmmunition(*ship);
    for (const Outfit *outfit : toRefill) {
      int amount =
          ship->Attributes().CanAdd(*outfit, numeric_limits<int>::max());
      if (amount > 0) {
        bool available = outfitter.Has(outfit) || player.Stock(outfit) > 0;
        available = available || player.Cargo().Get(outfit) ||
                    player.Storage().Get(outfit);
        if (available)
          needed[outfit] += amount;
      }
    }
  }

  int64_t cost = 0;
  for (auto &it : needed) {
    // Don't count cost of anything installed from cargo or storage.
    it.second = max(0, it.second - player.Cargo().Get(it.first) -
                           player.Storage().Get(it.first));
    if (!outfitter.Has(it.first))
      it.second = min(it.second, max(0, player.Stock(it.first)));
    cost += player.StockDepreciation().Value(it.first, day, it.second);
  }
  if (!needed.empty() && cost < player.Accounts().Credits()) {
    string message = "是否要为你的飞船补满全部弹药";
    message += (count == 1) ? "?" : "s?";
    if (cost)
      message += "费用为 " + Format::CreditString(cost) + "。";
    GetUI().Push(
        DialogPanel::CallFunctionIfOk(this, &OutfitterPanel::Refill, message));
  }
}

void OutfitterPanel::Refill() {
  for (const shared_ptr<Ship> &ship : player.Ships()) {
    // Skip ships in other systems and those that were unable to land in-system.
    if (ship->GetSystem() != player.GetSystem() || ship->IsDisabled())
      continue;

    auto toRefill = GetRefillableAmmunition(*ship);
    for (const Outfit *outfit : toRefill) {
      int neededAmmo =
          ship->Attributes().CanAdd(*outfit, numeric_limits<int>::max());
      if (neededAmmo > 0) {
        // Fill first from any stockpiles in storage.
        const int fromStorage = player.Storage().Remove(outfit, neededAmmo);
        neededAmmo -= fromStorage;
        // Then from cargo.
        const int fromCargo = player.Cargo().Remove(outfit, neededAmmo);
        neededAmmo -= fromCargo;
        // Then, buy at reduced (or full) price.
        int available =
            outfitter.Has(outfit)
                ? neededAmmo
                : min<int>(neededAmmo, max<int>(0, player.Stock(outfit)));
        if (neededAmmo && available > 0) {
          int64_t price =
              player.StockDepreciation().Value(outfit, day, available);
          player.Accounts().AddCredits(-price);
          player.AddStock(outfit, -available);
        }
        ship->AddOutfit(outfit, available + fromStorage + fromCargo);
      }
    }
  }
}

// Determine which ships of the selected ships should be referenced in this
// iteration of Buy / Sell.
const vector<Ship *> OutfitterPanel::GetShipsToOutfit(bool isInstall) const {
  vector<Ship *> shipsToOutfit;
  int compareValue = isInstall ? numeric_limits<int>::max() : 0;
  int compareMod = 2 * isInstall - 1;
  for (Ship *ship : playerShips) {
    if ((isInstall && !ShipCanAdd(ship, selectedOutfit)) ||
        (!isInstall && !ShipCanRemove(ship, selectedOutfit)))
      continue;

    int count = ship->OutfitCount(selectedOutfit);
    if (compareMod * count < compareMod * compareValue) {
      shipsToOutfit.clear();
      compareValue = count;
    }
    if (count == compareValue)
      shipsToOutfit.push_back(ship);
  }

  return shipsToOutfit;
}

int OutfitterPanel::FindItem(const string &text) const {
  int bestIndex = 9999;
  int bestItem = -1;
  auto it = zones.begin();
  for (unsigned int i = 0; i < zones.size(); ++i, ++it) {
    const Outfit *outfit = it->GetOutfit();
    int index = Format::Search(outfit->DisplayName(), text);
    if (index >= 0 && index < bestIndex) {
      bestIndex = index;
      bestItem = i;
      if (!index)
        return i;
    }
  }
  return bestItem;
}

double OutfitterPanel::ButtonPanelHeight() const {
  // The 70 = (3 x 10 (pad) + 20 x 2 (text)) for the credit and cargo space
  // information lines.
  return 70. + BUTTON_HEIGHT * 3 + BUTTON_ROW_PAD * 2;
}

void OutfitterPanel::DrawButtons() {
  // There will be two rows of buttons:
  //  [ Buy  ] [  Install  ] [  Cargo  ]
  //  [ Sell ] [ Uninstall ] [ Storage ]
  //                         [  Leave  ]
  const double rowOffsetY = BUTTON_HEIGHT + BUTTON_ROW_PAD;
  const double rowBaseY = Screen::BottomRight().Y() - 2 * rowOffsetY -
                          .5 * BUTTON_HEIGHT - BUTTON_ROW_START_PAD;
  const double buttonOffsetX = BUTTON_WIDTH + BUTTON_COL_PAD;
  const double buttonCenterX = Screen::Right() - SIDEBAR_WIDTH / 2 - 1.;
  const Point buttonSize{BUTTON_WIDTH, BUTTON_HEIGHT};

  // Draw the button panel (shop side panel footer).
  const Point buttonPanelSize(SIDEBAR_WIDTH, ButtonPanelHeight());
  const Rectangle buttonsFooter(Screen::BottomRight() - .5 * buttonPanelSize,
                                buttonPanelSize);
  FillShader::Fill(buttonsFooter,
                   *GameData::Colors().Get("shop side panel background"));
  FillShader::Fill(Point(Screen::Right() - SIDEBAR_WIDTH / 2,
                         Screen::Bottom() - ButtonPanelHeight()),
                   Point(SIDEBAR_WIDTH, 1),
                   *GameData::Colors().Get("shop side panel footer"));

  // Set up font size and colors for the credits.
  const Font &font = FontSet::Get(14);
  const Color &bright = *GameData::Colors().Get("bright");
  const Color &dim = *GameData::Colors().Get("medium");

  // Draw the row for credits display.
  const Point creditsPoint(Screen::Right() - SIDEBAR_WIDTH + 10,
                           Screen::Bottom() - ButtonPanelHeight() + 10);
  font.Draw("你拥有：", creditsPoint, dim);
  const string credits = Format::CreditString(player.Accounts().Credits());
  font.Draw({credits, {SIDEBAR_WIDTH - 20, Alignment::RIGHT}}, creditsPoint,
            bright);

  // Draw the row for Fleet Cargo Space free.
  const Point cargoPoint(Screen::Right() - SIDEBAR_WIDTH + 10,
                         Screen::Bottom() - ButtonPanelHeight() + 30);
  font.Draw("可用货舱：", cargoPoint, dim);
  string space = Format::Number(player.Cargo().Free()) + " / " +
                 Format::Number(player.Cargo().Size());
  font.Draw({space, {SIDEBAR_WIDTH - 20, Alignment::RIGHT}}, cargoPoint,
            bright);

  // Clear the buttonZones, they will be populated again as buttons are drawn.
  buttonZones.clear();

  // Row 1.
  DrawButton("_Buy",
             Rectangle(Point(buttonCenterX + buttonOffsetX * -1,
                             rowBaseY + rowOffsetY * 0),
                       buttonSize),
             ButtonActive('b'), hoverButton == 'b', 'b');
  DrawButton("_Install",
             Rectangle(Point(buttonCenterX + buttonOffsetX * 0,
                             rowBaseY + rowOffsetY * 0),
                       buttonSize),
             ButtonActive('i'), hoverButton == 'i', 'i');
  DrawButton("_Cargo",
             Rectangle(Point(buttonCenterX + buttonOffsetX * 1,
                             rowBaseY + rowOffsetY * 0),
                       buttonSize),
             ButtonActive('c'), hoverButton == 'c', 'c');
  // Row 2.
  DrawButton("_Sell",
             Rectangle(Point(buttonCenterX + buttonOffsetX * -1,
                             rowBaseY + rowOffsetY * 1),
                       buttonSize),
             ButtonActive('s'), hoverButton == 's', 's');
  DrawButton(
      !CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage) &&
              CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Storage)
          ? "_Unload"
          : "_Uninstall",
      Rectangle(
          Point(buttonCenterX + buttonOffsetX * 0, rowBaseY + rowOffsetY * 1),
          buttonSize),
      ButtonActive('u'), hoverButton == 'u', 'u');
  DrawButton("Sto_re",
             Rectangle(Point(buttonCenterX + buttonOffsetX * 1,
                             rowBaseY + rowOffsetY * 1),
                       buttonSize),
             ButtonActive('r'), hoverButton == 'r', 'r');
  // Row 3.
  DrawButton("_Leave",
             Rectangle(Point(buttonCenterX + buttonOffsetX * 1,
                             rowBaseY + rowOffsetY * 2),
                       buttonSize),
             true, hoverButton == 'l', 'l');

  // Draw the Modifier hover text that appears below the buttons when a modifier
  // is being applied.
  int modifier = Modifier();
  if (modifier > 1) {
    string mod = "x " + to_string(modifier);
    int modWidth = font.Width(mod);
    for (int i = -1; i < 2; ++i)
      font.Draw(
          mod,
          Point(buttonCenterX + buttonOffsetX * i, rowBaseY + rowOffsetY * 0) +
              Point(-.5 * modWidth, 10.),
          dim);
    for (int i = -1; i < 2; ++i)
      font.Draw(
          mod,
          Point(buttonCenterX + buttonOffsetX * i, rowBaseY + rowOffsetY * 1) +
              Point(-.5 * modWidth, 10.),
          dim);
  }

  // Draw tooltips for the button being hovered over:
  string tooltip = GameData::Tooltip(string("outfitter: ") + hoverButton);
  if (!tooltip.empty()) {
    buttonsTooltip.IncrementCount();
    if (buttonsTooltip.ShouldDraw()) {
      buttonsTooltip.SetZone(buttonsFooter);
      buttonsTooltip.SetText(tooltip, true);
      buttonsTooltip.Draw();
    }
  } else
    buttonsTooltip.DecrementCount();

  // Draw the tooltip for your full number of credits and free cargo space
  const Rectangle creditsBox =
      Rectangle::FromCorner(creditsPoint, Point(SIDEBAR_WIDTH - 20, 30));
  if (creditsBox.Contains(hoverPoint)) {
    creditsTooltip.IncrementCount();
    if (creditsTooltip.ShouldDraw()) {
      creditsTooltip.SetZone(creditsBox);
      creditsTooltip.SetText(
          Format::CreditString(player.Accounts().Credits(), false) + '\n' +
              Format::MassString(player.Cargo().Free()) + " 空余，共 " +
              Format::MassString(player.Cargo().Size()) + " 货舱总容量",
          true);
      creditsTooltip.Draw();
    }
  } else
    creditsTooltip.DecrementCount();
}

ShopPanel::TransactionResult OutfitterPanel::HandleShortcuts(SDL_Keycode key) {
  TransactionResult result = false;
  if (key == 'b') {
    // Buy and install up to <modifier> outfits for each selected ship.
    result = MoveOutfit(OutfitLocation::Shop, OutfitLocation::Ship,
                        "buy and install");
  } else if (key == 's') {
    // Sell <modifier> of the selected outfit from cargo, or else storage, or
    // else from each selected ship. Return a result based on the reason that
    // none can be sold from the selected ships.
    if (!MoveOutfit(OutfitLocation::Cargo, OutfitLocation::Shop) &&
        !MoveOutfit(OutfitLocation::Storage, OutfitLocation::Shop))
      result = MoveOutfit(OutfitLocation::Ship, OutfitLocation::Shop, "sell");
  } else if (key == 'r') {
    // Move <modifier> of the selected outfit to storage from either cargo or
    // else each of the selected ships.
    if (!MoveOutfit(OutfitLocation::Cargo, OutfitLocation::Storage))
      result =
          MoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage, "store");
  } else if (key == 'c') {
    // Either move up to <multiple> outfits into cargo from storage if any are
    // in storage, or else buy up to <modifier> outfits into cargo. Note: If the
    // outfit cannot be moved from storage or bought into cargo, give an error
    // based on the buy condition.
    if (!MoveOutfit(OutfitLocation::Storage, OutfitLocation::Cargo))
      result = MoveOutfit(OutfitLocation::Shop, OutfitLocation::Cargo,
                          "buy and load");
  } else if (key == 'i') {
    // Install up to <modifier> outfits from already owned equipment into each
    // selected ship.
    if (!MoveOutfit(OutfitLocation::Cargo, OutfitLocation::Ship))
      result =
          MoveOutfit(OutfitLocation::Storage, OutfitLocation::Ship, "install");
  } else if (key == 'u') {
    // Uninstall up to <multiple> outfits from each of the selected ships if any
    // are available to uninstall, or else unload up to <multiple> outfits from
    // cargo and place them storage. Note: If the outfit cannot be uninstalled
    // or unloaded, give an error based on the inability to uninstall the outfit
    // from any ship.
    result =
        MoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage, "uninstall");
    if (!result && MoveOutfit(OutfitLocation::Cargo, OutfitLocation::Storage))
      result = true;
  }

  return result;
}
