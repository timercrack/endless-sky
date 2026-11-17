/* OutfitterPanel.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "OutfitterPanel.h"

#include "text/Alignment.h"
#include "comparators/BySeriesAndIndex.h"
#include "Color.h"
#include "DialogPanel.h"
#include "text/DisplayText.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "GameData.h"
#include "Hardpoint.h"
#include "Mission.h"
#include "Outfit.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Point.h"
#include "Preferences.h"
#include "Rectangle.h"
#include "Screen.h"
#include "Ship.h"
#include "image/Sprite.h"
#include "image/SpriteSet.h"
#include "shader/SpriteShader.h"
#include "text/Truncate.h"
#include "UI.h"
#include "Weapon.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <ranges>

using namespace std;

namespace {
	// Label for the description field of the detail pane.
	const string DESCRIPTION = "description";
	const string LICENSE = " License";

	// Numeric parameters for the visibility checkboxes. Note the checkboxes already have some padding of their own.
	const Point checkboxSize{20, 20};
	const Point keyPad{5, 8};

	// Determine the refillable ammunition a particular ship consumes or stores.
	set<const Outfit *> GetRefillableAmmunition(const Ship &ship) noexcept
	{
		auto toRefill = set<const Outfit *>{};
		for(auto &&it : ship.Weapons())
		{
			const Weapon *weapon = it.GetWeapon();
			if(weapon && weapon->Ammo() && weapon->AmmoUsage() > 0)
				toRefill.emplace(weapon->Ammo());
		}

		// Carriers may be configured to supply ammunition for carried ships found
		// within the fleet. Since a particular ammunition outfit is not bound to
		// any particular weapon (i.e. one weapon may consume it, while another may
		// only require it be installed), we always want to restock these outfits.
		for(auto &&it : ship.Outfits())
		{
			const Outfit *outfit = it.first;
			const set<const Outfit *> &ammo = outfit->AmmoStored();
			if(!outfit->GetWeapon() && !ammo.empty())
				toRefill.insert(ammo.begin(), ammo.end());
		}
		return toRefill;
	}

	bool IsLicense(const string &name)
	{
		return name.ends_with(LICENSE);
	}

	string LicenseRoot(const string &name)
	{
		return name.substr(0, name.length() - LICENSE.length());
	}

	string LocationName(OutfitterPanel::OutfitLocation location)
	{
		switch(location)
		{
			case OutfitterPanel::OutfitLocation::Ship:
				return "飞船";
			case OutfitterPanel::OutfitLocation::Shop:
				return "商店";
			case OutfitterPanel::OutfitLocation::Cargo:
				return "货舱";
			case OutfitterPanel::OutfitLocation::Storage:
				return "存储";
			default:
				throw runtime_error("unreachable");
		}
	}
}



OutfitterPanel::OutfitterPanel(PlayerInfo &player, const Sale<Outfit> &stock)
	: ShopPanel(player, true), outfitter(stock)
{
	for(const pair<const string, Outfit> &it : GameData::Outfits())
		catalog[it.second.Category()].push_back(it.first);

	for(pair<const string, vector<string>> &it : catalog)
		sort(it.second.begin(), it.second.end(), BySeriesAndIndex<Outfit>());

	for(auto &ship : player.Ships())
		if(ship->GetPlanet() == planet)
			++shipsHere;
}



void OutfitterPanel::Step()
{
	CheckRefill();
	ShopPanel::Step();
	ShopPanel::CheckForMissions(Mission::OUTFITTER);
	ShopPanel::ValidateSelectedShips();
	if(GetUI().IsTop(this) && !checkedHelp)
		// Use short-circuiting to only display one of them at a time.
		// (The first valid condition encountered will make us skip the others.)
		if(DoHelp("outfitter") || DoHelp("cargo management") || DoHelp("uninstalling and storage")
				|| (shipsHere > 1 && DoHelp("outfitter with multiple ships")) || true)
			// Either a help message was freshly displayed, or all of them have already been seen.
			checkedHelp = true;
}



int OutfitterPanel::TileSize() const
{
	return OUTFIT_SIZE;
}



int OutfitterPanel::VisibilityCheckboxesSize() const
{
	return 4 * checkboxSize.Y() + 2. * keyPad.Y();
}



bool OutfitterPanel::HasItem(const string &name) const
{
	const Outfit *outfit = GameData::Outfits().Get(name);
	if(showForSale && (outfitter.Has(outfit) || player.Stock(outfit) > 0))
		return true;

	if(showCargo && player.Cargo().Get(outfit))
		return true;

	if(showStorage && player.Storage().Get(outfit))
		return true;

	if(showInstalled)
		for(const Ship *ship : playerShips)
			if(ship->OutfitCount(outfit))
				return true;

	if(showForSale && HasLicense(name))
		return true;

	return false;
}



void OutfitterPanel::DrawItem(const string &name, const Point &point)
{
	const Outfit *outfit = GameData::Outfits().Get(name);
	zones.emplace_back(point, Point(OUTFIT_SIZE, OUTFIT_SIZE), outfit);
	if(point.Y() + OUTFIT_SIZE / 2 < Screen::Top() || point.Y() - OUTFIT_SIZE / 2 > Screen::Bottom())
		return;

	bool isSelected = (outfit == selectedOutfit);
	bool isOwned = playerShip && playerShip->OutfitCount(outfit);
	DrawOutfit(*outfit, point, isSelected, isOwned);

	// Check if this outfit is a "license".
	bool isLicense = IsLicense(name);
	int mapSize = outfit->Get("map");

	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &highlight = *GameData::Colors().Get("outfitter difference highlight");

	bool highlightDifferences = false;
	if(playerShip || isLicense || mapSize)
	{
		int minCount = numeric_limits<int>::max();
		int maxCount = 0;
		if(isLicense)
			minCount = maxCount = player.HasLicense(LicenseRoot(name));
		else if(mapSize)
		{
			bool mapMinables = outfit->Get("map minables");
			minCount = maxCount = player.HasMapped(mapSize, mapMinables);
		}
		else
		{
			highlightDifferences = true;
			string firstModelName;
			for(const Ship *ship : playerShips)
			{
				// Highlight differences in installed outfit counts only when all selected ships are of the same model.
				string modelName = ship->TrueModelName();
				if(firstModelName.empty())
					firstModelName = modelName;
				else
					highlightDifferences &= (modelName == firstModelName);
				int count = ship->OutfitCount(outfit);
				minCount = min(minCount, count);
				maxCount = max(maxCount, count);
			}
		}

		if(maxCount)
		{
			string label = "已安装：" + to_string(minCount);
			Color color = bright;
			if(maxCount > minCount)
			{
				label += " - " + to_string(maxCount);
				if(highlightDifferences)
					color = highlight;
			}

			Point labelPos = point + Point(-OUTFIT_SIZE / 2 + 20, OUTFIT_SIZE / 2 - 38);
			font.Draw(label, labelPos, color);
		}
	}

	// Don't show the "in stock" amount if the outfit has an unlimited stock.
	int stock = 0;
	if(!outfitter.Has(outfit))
		stock = max(0, player.Stock(outfit));
	int cargo = player.Cargo().Get(outfit);
	int storage = player.Storage().Get(outfit);

	string message;
	if(cargo && storage && stock)
		message = "货舱+存储：" + to_string(cargo + storage) + "，库存：" + to_string(stock);
	else if(cargo && storage)
		message = "货舱：" + to_string(cargo) + "，存储：" + to_string(storage);
	else if(cargo && stock)
		message = "货舱：" + to_string(cargo) + "，库存：" + to_string(stock);
	else if(storage && stock)
		message = "存储：" + to_string(storage) + "，库存：" + to_string(stock);
	else if(cargo)
		message = "货舱：" + to_string(cargo);
	else if(storage)
		message = "存储：" + to_string(storage);
	else if(stock)
		message = "库存：" + to_string(stock);
	else if(!outfitter.Has(outfit))
		message = "(not sold here)";
	if(!message.empty())
	{
		Point pos = point + Point(
			OUTFIT_SIZE / 2 - 20 - font.Width(message),
			OUTFIT_SIZE / 2 - 24);
		font.Draw(message, pos, bright);
	}
}



double OutfitterPanel::DrawDetails(const Point &center)
{
	string selectedItem = "Nothing Selected";
	const Font &font = FontSet::Get(14);

	double heightOffset = 20.;

	if(selectedOutfit)
	{
		outfitInfo.Update(*selectedOutfit, player,
			CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Shop) ||
			CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Shop) ||
			CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Shop),
			collapsed.contains(DESCRIPTION));
		selectedItem = selectedOutfit->DisplayName();

		const Drawable &thumbnail = selectedOutfit->Thumbnail();
		const Sprite *thumbnailSprite = thumbnail.GetSprite();
		const float tileSize = thumbnailSprite
			? max(thumbnailSprite->Height(), static_cast<float>(TileSize()))
			: static_cast<float>(TileSize());
		const Point thumbnailCenter(center.X(), center.Y() + 20 + static_cast<int>(tileSize / 2));
		const Point startPoint(center.X() - INFOBAR_WIDTH / 2 + 20, center.Y() + 20 + tileSize);

		const Sprite *background = SpriteSet::Get("ui/outfitter unselected");
		SpriteShader::Draw(background, thumbnailCenter);
		DrawThumbnail(thumbnail, true, thumbnailCenter);

		const bool hasDescription = outfitInfo.DescriptionHeight();

		double descriptionOffset = hasDescription ? 40. : 0.;

		if(hasDescription)
		{
			if(!collapsed.contains(DESCRIPTION))
			{
				descriptionOffset = outfitInfo.DescriptionHeight();
				outfitInfo.DrawDescription(startPoint);
			}
			else
			{
				const Color &dim = *GameData::Colors().Get("medium");
				font.Draw(DESCRIPTION, startPoint + Point(35., 12.), dim);
				const Sprite *collapsedArrow = SpriteSet::Get("ui/collapsed");
				SpriteShader::Draw(collapsedArrow, startPoint + Point(20., 20.));
			}

			// Calculate the ClickZone for the description and add it.
			const Point descriptionDimensions(INFOBAR_WIDTH, descriptionOffset);
			const Point descriptionCenter(center.X(), startPoint.Y() + descriptionOffset / 2);
			ClickZone<string> collapseDescription = ClickZone<string>(
				descriptionCenter, descriptionDimensions, DESCRIPTION);
			categoryZones.emplace_back(collapseDescription);
		}

		const Point requirementsPoint(startPoint.X(), startPoint.Y() + descriptionOffset);
		const Point attributesPoint(startPoint.X(), requirementsPoint.Y() + outfitInfo.RequirementsHeight());
		outfitInfo.DrawRequirements(requirementsPoint);
		outfitInfo.DrawAttributes(attributesPoint);

		heightOffset = attributesPoint.Y() + outfitInfo.AttributesHeight();
	}

	// Draw this string representing the selected item (if any), centered in the details side panel.
	const Color &bright = *GameData::Colors().Get("bright");
	Point selectedPoint(center.X() - INFOBAR_WIDTH / 2, center.Y());
	font.Draw({selectedItem, {INFOBAR_WIDTH, Alignment::CENTER, Truncate::MIDDLE}},
		selectedPoint, bright);

	return heightOffset;
}



ShopPanel::TransactionResult OutfitterPanel::CanMoveOutfit(OutfitLocation fromLocation, OutfitLocation toLocation,
	const string &actionName) const
{
	if(!planet || !selectedOutfit)
		return "未选择装备。";

	// Prevent coding up bad combinations.
	if(fromLocation == toLocation)
		throw runtime_error("unreachable; to and from are the same");

	// Handle special cases such as maps and licenses.
	int mapSize = selectedOutfit->Get("map");
	if(mapSize)
	{
		if(fromLocation != OutfitLocation::Shop)
			return "你无法" + actionName + "星图。星图一经购买便永久归你所有。";
		if(toLocation == OutfitLocation::Cargo || toLocation == OutfitLocation::Storage)
			return "你无法将星图放入" + LocationName(toLocation) + "。";
		bool mapMinables = selectedOutfit->Get("map minables");
		if(mapSize > 0 && player.HasMapped(mapSize, mapMinables))
			return "此星图所示的星系你都已绘入星图，无需再买一份。";
		return true;
	}

	if(IsLicense(selectedOutfit->TrueName()))
	{
		if(HasLicense(selectedOutfit->TrueName()))
		{
			if(fromLocation != OutfitLocation::Shop)
				return "你无法" + actionName + "许可证。许可证一经获得便永久归你所有。";
			if(toLocation == OutfitLocation::Cargo || toLocation == OutfitLocation::Storage)
				return "你无法将许可证放入" + LocationName(toLocation) + "。";
			return "你已拥有此许可证，无需重复购买。";
		}
		if(fromLocation != OutfitLocation::Shop)
			return "你无法" + actionName + "许可证。";
		if(toLocation != OutfitLocation::Ship)
			return "你无法将许可证放入" + LocationName(toLocation) + "。";
		return true;
	}

	bool canSource = false;
	bool canPlace = false;

	// Handle reasons why the outfit may not be moved from fromLocation.
	switch(fromLocation)
	{
		case OutfitLocation::Ship:
		{
			if(!playerShip)
				return "未选择飞船。";
			if(selectedOutfit->GetPrecise("unremovable"))
				return "此装备一经安装便无法拆除。";

			vector<pair<string, vector<string>>> dependentOutfitErrors;
			vector<string> errorDetails;
			bool foundOutfit = false;
			for(const Ship *ship : playerShips)
			{
				if(ship->OutfitCount(selectedOutfit) > 0)
				{
					foundOutfit = true;
					Outfit attributes = ship->Attributes();
					// If this outfit has linked outfits, check if we could sell it
					// if we sold all the linked outfits first.
					for(const Outfit *linked : selectedOutfit->LinkedOutfits())
						if(ship->OutfitCount(linked))
							attributes.Add(*linked, -ship->OutfitCount(linked));
					// Linked outfits are not a factor (now), check whether this ship can uninstall this outfit.
					auto [canAdd, failureReasons] = attributes.CanAddWithReasons(*selectedOutfit, -1);
					canSource = canAdd;

					// If we have an outfit that can be sourced, break out, otherwise return an appropriate error.
					if(canSource)
						break;

					// At least one of the outfit is installed on at least one of the selected ships.
					// Create a complete summary of reasons why none of this outfit were able to be <verb>'d:
					// Looping over each ship which has the selected outfit, identify the reasons why it cannot be
					// <verb>'d. Make a list of ship to errors to assemble into a string afterward.
					vector<string> offendingOutfits;
					for(const auto &outfit : ship->Outfits() | views::keys)
						for(const string &reason : failureReasons)
							if(outfit->GetPrecise(reason) < 0)
							{
								offendingOutfits.push_back("“" + outfit->DisplayName() + "”");
								break;
							}
					if(offendingOutfits.size() == 1)
					{
						errorDetails.emplace_back(offendingOutfits.back() + "依赖此装备，必须先将其卸载");
					}
					else if(offendingOutfits.size() > 1)
					{
						errorDetails.emplace_back(Format::List(offendingOutfits) + "依赖此装备，"
							"必须先卸载其中至少一件");
					}
					// If there is no offending outfit, then the attribute is directly on the ship.
					else if(failureReasons.size() == 1)
					{
						errorDetails.emplace_back("属性“" + failureReasons.back() + "”的数值将低于其允许的最小值");
					}
					else
					{
						auto ListFunc = [](const string &reason) -> string {
							return "“" + reason + "”";
						};
						errorDetails.emplace_back("属性" + Format::List(failureReasons, ListFunc)
							+ "的数值将低于其允许的最小值");
					}

					dependentOutfitErrors.emplace_back(ship->GivenName(), std::move(errorDetails));
				}
			}

			if(canSource)
				break;

			// The outfit cannot be installed from any ship.
			if(!foundOutfit)
				return {canSource, canPlace,
					"你没有可" + actionName + "的" + selectedOutfit->PluralName() + "。"};

			// Return the errors in the appropriate format.
			if(!dependentOutfitErrors.empty())
			{
				string errorMessage = "你无法从"s +
					(playerShips.size() > 1 ? "任何一艘选中的飞船" : "你的飞船") + "上" + actionName
					+ "此装备，原因如下：\n";
				int i = 0;
				for(const auto &[shipName, errors] : dependentOutfitErrors)
				{
					if(playerShips.size() > 1)
					{
						errorMessage += to_string(++i) + ". 你无法从“" + shipName + "”上" + actionName
							+ "此装备，原因如下：\n";
					}
					for(const string &error : errors)
						errorMessage += "- " + error + '\n';
				}
				return {canSource, canPlace, errorMessage};
			}

			break;
		}
		case OutfitLocation::Shop:
		{
			// If outfit is not available in the Outfitter, respond that it can't be bought here.
			if(!(outfitter.Has(selectedOutfit) || player.Stock(selectedOutfit) > 0))
			{
				return {canSource, canPlace,
					"你无法在此购买此装备。它之所以出现在列表中，只是因为你已拥有此装备，"
					"但这里并不出售。"};
			}

			// Check special unique outfits, if you already have them.
			// Skip this for speed if this action is not a buy and install action.
			if(toLocation != OutfitLocation::Ship)
			{
				bool mapMinables = selectedOutfit->Get("map minables");
				if(mapSize > 0 && player.HasMapped(mapSize, mapMinables))
					return {canSource, canPlace,
						"此星图所示的星系你都已绘入星图，无需再买一份。"};

				if(HasLicense(selectedOutfit->TrueName()))
					return {canSource, canPlace,
						"你已拥有此许可证，无需重复购买。"};
			}

			// Determine what you will have to pay to buy this outfit.
			int64_t cost = player.StockDepreciation().Value(selectedOutfit, day);
			int64_t credits = player.Accounts().Credits();
			if(cost > credits)
				return {canSource, canPlace,
					"你的资金不足以购买此装备，还差 " +
					Format::CreditString(cost - credits) + "。"};

			// Add the cost to buy the required license.
			int64_t licenseCost = LicenseCost(selectedOutfit, false);
			if(cost + licenseCost > credits)
				return {canSource, canPlace,
					"你的资金不足以购买此装备，因为你还需要购买它所需的许可证。还差 " +
					Format::CreditString(cost + licenseCost - credits) + "。"};

			// Check that the player has any necessary licenses.
			if(licenseCost < 0)
				return {canSource, canPlace,
					"你无法购买此装备，因为它需要你尚未拥有的许可证。"};

			// The outfit can be purchased (available in the outfitter, licensed and affordable).
			canSource = true;
			break;
		}
		case OutfitLocation::Cargo:
		{
			// Do we have any in cargo?
			if(!player.Cargo().Get(selectedOutfit))
				return {canSource, canPlace,
					"你的货舱中没有可" + actionName + "的" + selectedOutfit->PluralName() + "。"};
			canSource = true;
			break;
		}
		case OutfitLocation::Storage:
		{
			// Do we have any in storage?
			if(!player.Storage().Get(selectedOutfit))
				return {canSource, canPlace,
					"你的存储中没有可" + actionName + "的" + selectedOutfit->PluralName() + "。"};
			canSource = true;
			break;
		}
		default:
			throw runtime_error("unreachable");
	}

	// Collect relevant errors.
	vector<string> errors;

	// Handle reasons why the outfit may not be moved to toLocation.
	switch(toLocation)
	{
		case OutfitLocation::Ship:
		{
			if(!playerShip)
				return "未选择飞船。";

			if(selectedOutfit->GetPrecise("uninstallable"))
				return "此物品不是可安装在飞船上的装备。";

			// Find if any ship can install the outfit.
			for(const Ship *ship : playerShips)
				if(ShipCanAdd(ship, selectedOutfit))
				{
					canPlace = true;
					break;
				}

			if(!canPlace)
			{
				auto TonsFree = [](double needed, double space, const string &name) -> string {
					return "你无法安装此装备，因为它需要占用 "
						+ Format::MassString(needed) + name + "，而这艘飞船只剩 "
						+ Format::MassString(space) + "。";
				};

				// If no selected ship can install the outfit, report error based on playerShip.
				double outfitNeeded = -selectedOutfit->Get("outfit space");
				double outfitSpace = playerShip->Attributes().Get("outfit space");
				if(outfitNeeded > outfitSpace)
					errors.push_back(TonsFree(outfitNeeded, outfitSpace, "装备空间"));

				double weaponNeeded = -selectedOutfit->Get("weapon capacity");
				double weaponSpace = playerShip->Attributes().Get("weapon capacity");
				if(weaponNeeded > weaponSpace)
					errors.push_back("你飞船的装备容量中只有一部分可用于武器。"
						+ TonsFree(weaponNeeded, weaponSpace, "武器空间"));

				double engineNeeded = -selectedOutfit->Get("engine capacity");
				double engineSpace = playerShip->Attributes().Get("engine capacity");
				if(engineNeeded > engineSpace)
					errors.push_back("你飞船的装备容量中只有一部分可用于引擎。"
						+ TonsFree(engineNeeded, engineSpace, "引擎空间"));

				double cargoNeeded = -selectedOutfit->Get("cargo space");
				double cargoSpace = playerShip->Attributes().Get("cargo space");
				if(cargoNeeded > cargoSpace)
					errors.push_back(TonsFree(cargoNeeded, cargoSpace, "货舱空间"));

				int mountsNeeded = -selectedOutfit->Get("turret mounts");
				int mountsFree = playerShip->Attributes().Get("turret mounts");
				if(mountsNeeded > mountsFree)
					errors.emplace_back("此武器需要安装在炮塔座上，"
						"但你的飞船没有空闲的炮塔座。");

				int gunsNeeded = -selectedOutfit->Get("gun ports");
				int gunsFree = playerShip->Attributes().Get("gun ports");
				if(gunsNeeded > gunsFree)
					errors.emplace_back("此武器需要安装在炮位上，"
						"但你的飞船没有空闲的炮位。");

				// If the outfit category is ammo, the problem wasn't one of the above attributes, and this outfit
				// could never have its installation limited by usual means, then assume that the issue is a lack
				// of ammo storage.
				if(errors.empty() && selectedOutfit->Category() == "Ammunition" && !outfitNeeded && !weaponNeeded
						&& !engineNeeded && !cargoNeeded && !mountsNeeded && !gunsNeeded)
					errors.emplace_back(!playerShip->OutfitCount(selectedOutfit) ?
						"此装备是武器弹药。"
						"你必须先安装相应的武器才能安装它。"
						: "你已拥有此武器的最大弹药量。"
						"如需安装更多弹药，必须先安装更多弹药存储装置"
						"或另一件此类武器。");

				// Handle other attributes more generically, if none of the above are the problem.
				if(errors.empty())
				{
					vector<string> reasons = playerShip->Attributes().CanAddWithReasons(*selectedOutfit, 1).second;
					bool isAutomaton = playerShip->Attributes().GetPrecise("automaton")
						|| selectedOutfit->GetPrecise("automaton");
					string error = "你无法安装此装备，因为它需要：\n";
					for(const string &reason : reasons)
					{
						// This attribute will have a lower limit, as otherwise it couldn't have been the reason
						// the outfit couldn't be installed.
						optional<double> minimum = Outfit::LowerLimit(reason, isAutomaton);
						double shipAvailable = playerShip->Attributes().Get(reason) - minimum.value();
						double outfitRequires = -selectedOutfit->Get(reason);
						error += "\t- " + OutfitInfoDisplay::FormatAttribute(reason, outfitRequires) + " “" + reason
							+ "”，而这艘飞船只剩 " + OutfitInfoDisplay::FormatAttribute(reason, shipAvailable);
					}
					errors.push_back(error);
				}

				// For unhandled outfit requirements, show a catch-all error message.
				if(errors.empty())
					errors.push_back("你无法在你的飞船上安装此装备。");

				// Return the errors in the appropriate format.
				if(errors.size() == 1)
					return {canSource, canPlace, errors[0]};
				string errorMessage = "你无法" + actionName + "此装备，原因如下：\n";
				for(const string &error : errors)
					errorMessage += "- " + error + '\n';
				return {canSource, canPlace, errorMessage};
			}
			break;
		}
		case OutfitLocation::Shop:
		{
			// Can sell anything to the shop that can be removed from elsewhere.
			canPlace = true;
			break;
		}
		case OutfitLocation::Cargo:
		{
			// Check fleet cargo space vs outfit mass.
			double mass = selectedOutfit->Mass();
			double freeCargo = player.Cargo().FreePrecise();
			int largestHold = player.LargestCargoHold();
			bool couldFit = (mass <= largestHold);
			if(!mass || (couldFit && freeCargo >= mass))
				canPlace = true;
			else
			{
				string message = "你无法将此装备装入货舱，因为它的质量为 "
					+ Format::MassString(mass) + "，而你的舰队";
				if(couldFit)
					message += "只剩 " + Format::MassString(freeCargo) + "货舱空间。";
				else
					message += "中最大的货舱也只有 " + Format::MassString(largestHold) + "。";
				return {canSource, canPlace, message};
			}
			break;
		}
		case OutfitLocation::Storage:
		{
			// Can store anything in storage that can be removed from elsewhere.
			canPlace = true;
			break;
		}
		default:
			throw runtime_error("unreachable");
	}

	return canSource && canPlace;
}



ShopPanel::TransactionResult OutfitterPanel::MoveOutfit(OutfitLocation fromLocation, OutfitLocation toLocation,
	const string &actionName) const
{
	// Source up outfits from the <fromLocation> and move them to the specified <toLocation>. If ships are the to/from
	// location, then each ship will install/uninstall up to <howManyPer> outfits each, as allowed. Otherwise, it's
	// simply how many per shop/hold when ships are not involved in the move.

	// Note: CanMoveOutfit must be checked prior to further execution.
	TransactionResult canMove = CanMoveOutfit(fromLocation, toLocation, actionName);
	if(!canMove)
		return canMove;

	// The count of how many outfits will be moved will be per ship when ships are involved, otherwise simply per hold.
	// Hence, the concept of how many "per" rather than how many in total.
	const string &quantity = selectedQuantity->Text();
	int howManyPer = quantity.empty() ? 1 : stoi(quantity);

	// Purchases are handled here.
	if(fromLocation == OutfitLocation::Shop)
	{
		// Buy the required license.
		int64_t licenseCost = LicenseCost(selectedOutfit, false);
		if(licenseCost)
		{
			player.Accounts().AddCredits(-licenseCost);
			for(const string &licenseName : selectedOutfit->Licenses())
				if(!player.HasLicense(licenseName))
					player.AddLicense(licenseName);
		}

		// Special case: maps.
		int mapSize = selectedOutfit->Get("map");
		if(mapSize)
		{
			bool mapMinables = selectedOutfit->Get("map minables");
			player.Map(mapSize, mapMinables);
			player.Accounts().AddCredits(-selectedOutfit->Cost());
			return true;
		}

		// Special case: licenses.
		if(IsLicense(selectedOutfit->TrueName()))
		{
			player.AddLicense(LicenseRoot(selectedOutfit->TrueName()));
			player.Accounts().AddCredits(-selectedOutfit->Cost());
			return true;
		}

		// Buy up to <multiplier> outfits in total from the shop.
		int64_t cost = player.StockDepreciation().Value(selectedOutfit, day);

		if(toLocation == OutfitLocation::Ship)
		{
			// Buy and install on the selected ships.
			for(int i = howManyPer; i && (outfitter.Has(selectedOutfit) || player.Stock(selectedOutfit) > 0) &&
				cost <= player.Accounts().Credits(); --i)
			{
				// Find the ships with the fewest number of these outfits.
				const vector<Ship *> shipsToOutfit = GetShipsToOutfit(true);
				if(shipsToOutfit.empty())
					break;
				for(Ship *ship : shipsToOutfit)
				{
					if(!(outfitter.Has(selectedOutfit) || player.Stock(selectedOutfit) > 0)
							|| cost > player.Accounts().Credits())
						// Out of stock or money.
						break;

					// Pay for it and remove it from available stock.
					player.Accounts().AddCredits(-cost);
					player.AddStock(selectedOutfit, -1);
					cost = player.StockDepreciation().Value(selectedOutfit, day);

					// Install it on this ship.
					ship->AddOutfit(selectedOutfit, 1);
					int required = selectedOutfit->Get("required crew") + selectedOutfit->Get("mandatory crew");
					if(required && ship->Crew() + required <= static_cast<int>(ship->Attributes().Get("bunks")))
						ship->AddCrew(required);
					ship->Recharge();
				}
			}
		}
		else if(toLocation == OutfitLocation::Cargo || toLocation == OutfitLocation::Storage)
		{
			bool toCargo = toLocation == OutfitLocation::Cargo;
			CargoHold &storeIn = toCargo ? player.Cargo() : player.Storage();
			if(!outfitter.Has(selectedOutfit))
				howManyPer = min(howManyPer, player.Stock(selectedOutfit));
			if(toCargo)
			{
				// Buy up to <modifier> of the selected outfit and place them in fleet cargo.
				double mass = selectedOutfit->Mass();
				if(mass)
					howManyPer = min(howManyPer, static_cast<int>(storeIn.FreePrecise() / mass));
			}

			// How much will it cost to buy all that we can fit?
			int64_t price = player.StockDepreciation().Value(selectedOutfit, day, howManyPer);

			// Adjust the number to buy to stay within the player's available credits.
			while(price > player.Accounts().Credits() && howManyPer)
			{
				// The current cost is more than the amount of money the player has, so try a smaller amount.
				if(!--howManyPer)
					// If the smaller amount becomes zero, we're not able to do anything with cargo.
					return "资金不足，无法购买此物品并装入货舱。";
				price = player.StockDepreciation().Value(selectedOutfit, day, howManyPer);
			}

			if(howManyPer)
			{
				// Buy as many as can be paid for and will fit.
				player.Accounts().AddCredits(-price);
				player.AddStock(selectedOutfit, -howManyPer);

				// Put them into fleet cargo or planetary storage.
				storeIn.Add(selectedOutfit, howManyPer);
			}
		}
	}
	else if(fromLocation == OutfitLocation::Ship)
	{
		for(int i = howManyPer; i; --i)
		{
			// Get the ships that have the most of this outfit installed.
			const vector<Ship *> shipsToOutfit = GetShipsToOutfit();
			// Note: to get here, we have already confirmed that every ship in the selection
			// has the outfit installed and that the outfit can be uninstalled in the first place.
			if(shipsToOutfit.empty())
				break;
			for(Ship *ship : shipsToOutfit)
			{
				// Uninstall the outfit.
				ship->AddOutfit(selectedOutfit, -1);
				int required = selectedOutfit->Get("required crew") + selectedOutfit->Get("mandatory crew");
				if(required)
					ship->AddCrew(-required);
				// Adjust hired crew counts.
				ship->Recharge();

				if(toLocation == OutfitLocation::Shop)
				{
					// Do the sale.
					int64_t price = player.FleetDepreciation().Value(selectedOutfit, day);
					player.Accounts().AddCredits(price);
					player.AddStock(selectedOutfit, 1);
				}
				// If the context is uninstalling, move the outfit into Cargo or Storage.
				else if(toLocation == OutfitLocation::Cargo)
				{
					if(player.Cargo().FreePrecise() > selectedOutfit->Mass())
						player.Cargo().Add(selectedOutfit, 1);
					else
					{
						// If the player's cargo has run out of room, start moving
						// outfits into storage.
						toLocation = OutfitLocation::Storage;
						player.Storage().Add(selectedOutfit, 1);
					}
				}
				else if(toLocation == OutfitLocation::Storage)
					player.Storage().Add(selectedOutfit, 1);

				// Move linked outfits to storage.
				// Since some outfits have linked outfits, remove any that must also be moved as there
				// aren't enough supporting slots for said outfits once this outfit is removed.
				for(const Outfit *linked : selectedOutfit->LinkedOutfits())
				{
					if(!ship->OutfitCount(linked))
						continue;
					// Determine how many of this outfit we must uninstall to also uninstall the primary outfit.
					// Use the precise attribute values as to avoid float precision errors resulting in an
					// incorrect number of outfits being uninstalled.
					int mustUninstall = 0;
					const Outfit &attributes = ship->Attributes();
					// Iterate over the attributes of the linked outfit instead of the attributes of the ship,
					// since an outfit can never impact attributes it doesn't have.
					bool isAutomaton = ship->Attributes().GetPrecise("automaton")
						|| linked->GetPrecise("automaton");
					for(const auto &[name, linkedValue] : linked->Precise())
					{
						int64_t value = attributes.GetPrecise(name);
						// Only values on the ship that are below their allowed minimum need rectified.
						// Only attributes on this linked outfit that are negative can bring the attribute
						// above the minimum when removed.
						optional<int64_t> limit = Outfit::LowerLimitPrecise(name, isAutomaton);
						if(!limit.has_value() || value >= limit.value() || linkedValue >= 0)
							continue;

						// Determine how many of this outfit must be uninstalled to get back above the lower limit.
						// Using integer division, so we need to check the remainder to determine if we should
						// round up.
						auto [quotient, remainder] = std::div(value - limit.value(), linkedValue);
						mustUninstall = max<int>(mustUninstall, quotient + (remainder ? 1 : 0));
					}

					if(mustUninstall)
					{
						// AddOutfit ensures that we don't uninstall more than is actually installed.
						mustUninstall = -ship->AddOutfit(linked, -mustUninstall);

						if(toLocation == OutfitLocation::Shop)
						{
							// Do the sale of the outfit's linked outfit.
							int64_t price = player.FleetDepreciation().Value(linked, day, mustUninstall);
							player.Accounts().AddCredits(price);
							player.AddStock(linked, mustUninstall);
						}
						// If the context is uninstalling, move the outfit's linked outfit into Cargo or Storage.
						else if(toLocation == OutfitLocation::Cargo)
						{
							int movable = mustUninstall;
							while(player.Cargo().FreePrecise() < linked->Mass() * movable)
								--movable;
							if(movable)
								player.Cargo().Add(linked, movable);
							int remaining = mustUninstall - movable;
							if(remaining)
							{
								// If the player's cargo has run out of room, start moving
								// outfits into storage.
								toLocation = OutfitLocation::Storage;
								player.Storage().Add(linked, remaining);
							}
						}
						else if(toLocation == OutfitLocation::Storage)
							player.Storage().Add(linked, mustUninstall);
					}
				}
			}
		}
	}
	else if(fromLocation == OutfitLocation::Storage || fromLocation == OutfitLocation::Cargo)
	{
		CargoHold &hold = fromLocation == OutfitLocation::Cargo ? player.Cargo() : player.Storage();
		CargoHold &otherHold = fromLocation == OutfitLocation::Cargo ? player.Storage() : player.Cargo();

		if(toLocation == OutfitLocation::Shop)
		{
			// Do the sale.
			howManyPer = hold.Remove(selectedOutfit, howManyPer);
			int64_t price = player.FleetDepreciation().Value(selectedOutfit, day, howManyPer);
			player.Accounts().AddCredits(price);
			player.AddStock(selectedOutfit, howManyPer);
		}
		else if(toLocation == OutfitLocation::Ship)
		{
			for(int i = howManyPer; i && hold.Get(selectedOutfit) ; --i)
			{
				// Find the ships with the fewest number of these outfits.
				const vector<Ship *> shipsToOutfit = GetShipsToOutfit(true);
				if(shipsToOutfit.empty())
					break;
				for(Ship *ship : shipsToOutfit)
				{
					// If there were no more outfits in cargo, bail out.
					if(!hold.Remove(selectedOutfit))
						break;

					// Install it on this ship.
					ship->AddOutfit(selectedOutfit, 1);
					int required = selectedOutfit->Get("required crew") + selectedOutfit->Get("mandatory crew");
					if(required && ship->Crew() + required <= static_cast<int>(ship->Attributes().Get("bunks")))
						ship->AddCrew(required);
					ship->Recharge();
				}
			}
		}
		else
			// Move up to <modifier> from storage to cargo or cargo to storage (hold to otherHold).
			hold.Transfer(selectedOutfit, howManyPer, otherHold);
	}

	return canMove;
}



bool OutfitterPanel::ButtonActive(char key, bool shipRelatedOnly)
{
	if(key == 'b')
		return static_cast<bool>(CanMoveOutfit(OutfitLocation::Shop, OutfitLocation::Ship));
	if(key == 'i')
		return CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Ship)
			|| CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Ship);
	if(key == 'c')
		return !shipRelatedOnly && (CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Cargo) ||
			CanMoveOutfit(OutfitLocation::Shop, OutfitLocation::Cargo));
	if(key == 's')
		return (!shipRelatedOnly && (CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Shop) ||
			CanMoveOutfit(OutfitLocation::Storage, OutfitLocation::Shop))) ||
			CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Shop);
	if(key == 'u')
		return CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Cargo)
			|| CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage);
	if(key == 'r')
		return CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage) ||
			(!shipRelatedOnly && (CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Storage)
			|| CanMoveOutfit(OutfitLocation::Shop, OutfitLocation::Storage)));
	return false;
}



bool OutfitterPanel::ShouldHighlight(const Ship *ship)
{
	if(!selectedOutfit)
		return false;

	if(!ButtonActive(hoverButton, true))
		return false;

	// If we're hovering above a button that can add outfits to a ship then highlight the ship.
	if(hoverButton == 'b' || hoverButton == 'i')
		return ShipCanAdd(ship, selectedOutfit);

	// Otherwise, not installing, highlight ships which can have outfits removed.
	return ShipCanRemove(ship, selectedOutfit);
}



// Draw the display filter selection checkboxes in the lower left of the outfit panel.
void OutfitterPanel::DrawKey()
{
	const Sprite *back = SpriteSet::Get("ui/outfitter key");

	SpriteShader::Draw(back, Screen::BottomLeft() + .5 * Point(back->Width(), -back->Height()));

	const Font &font = FontSet::Get(14);
	const Color color[2] = {*GameData::Colors().Get("medium"), *GameData::Colors().Get("bright")};
	const Sprite *box[2] = {SpriteSet::Get("ui/unchecked"), SpriteSet::Get("ui/checked")};

	Point pos = Screen::BottomLeft() +
		Point{keyPad.X() + .5 * checkboxSize.X(), -VisibilityCheckboxesSize() + .5 * checkboxSize.Y() + keyPad.Y()};
	const Point labelOffset{.5 * checkboxSize.X(), -.5 * font.Height()};
	const Point activeAreaSize{180., checkboxSize.Y()};
	// Include the label and the sprite (20px) in the active checkbox area.
	const Point checkboxOffset{.5 * activeAreaSize.X() - .5 * checkboxSize.X(), 0.};

	SpriteShader::Draw(box[showForSale], pos);
	font.Draw("Show outfits for sale", pos + labelOffset, color[showForSale]);
	AddZone(Rectangle(pos + checkboxOffset, activeAreaSize), [this](){ ToggleForSale(); });

	pos.Y() += checkboxSize.Y();
	SpriteShader::Draw(box[showInstalled], pos);
	// The text color will be "medium" when no ships are selected, regardless of checkmark state,
	// indicating that the selection is invalid (invalid context).
	font.Draw("Show outfits installed", pos + labelOffset, color[showInstalled && playerShip]);
	AddZone(Rectangle(pos + checkboxOffset, activeAreaSize), [this]() { ToggleInstalled(); });

	pos.Y() += checkboxSize.Y();
	SpriteShader::Draw(box[showCargo], pos);
	font.Draw("Show outfits in cargo", pos + labelOffset, color[showCargo]);
	AddZone(Rectangle(pos + checkboxOffset, activeAreaSize), [this](){ ToggleCargo(); });

	pos.Y() += checkboxSize.Y();
	SpriteShader::Draw(box[showStorage], pos);
	font.Draw("Show outfits in storage", pos + labelOffset, color[showStorage]);
	AddZone(Rectangle(pos + checkboxOffset, activeAreaSize), [this](){ ToggleStorage(); });
}



optional<Rectangle> OutfitterPanel::KeyArea() const
{
	const Sprite *back = SpriteSet::Get("ui/outfitter key");
	return Rectangle::FromCorner(Screen::BottomLeft() + Point(0., -back->Height()),
		Point(back->Width(), back->Height()));
}



void OutfitterPanel::ToggleForSale()
{
	showForSale = !showForSale;

	CheckSelection();
	delayedAutoScroll = true;
}



void OutfitterPanel::ToggleInstalled()
{
	showInstalled = !showInstalled;

	CheckSelection();
	delayedAutoScroll = true;
}



void OutfitterPanel::ToggleStorage()
{
	showStorage = !showStorage;

	CheckSelection();
	delayedAutoScroll = true;
}



void OutfitterPanel::ToggleCargo()
{
	showCargo = !showCargo;

	CheckSelection();
	delayedAutoScroll = true;
}



// Returns true if this ship can install the selected outfit.
bool OutfitterPanel::ShipCanAdd(const Ship *ship, const Outfit *outfit)
{
	return (ship->Attributes().CanAdd(*outfit, 1) > 0);
}



// Returns true if this ship can uninstall the selected outfit.
bool OutfitterPanel::ShipCanRemove(const Ship *ship, const Outfit *outfit)
{
	if(!ship->OutfitCount(outfit))
		return false;

	// If this outfit has linked outfits, check if we could sell it if we sold all
	// the linked outfits for it first.
	const set<const Outfit *> &linkedOutfits = outfit->LinkedOutfits();
	if(!linkedOutfits.empty())
	{
		Outfit attributes = ship->Attributes();
		for(const Outfit *linked : outfit->LinkedOutfits())
		{
			int available = ship->OutfitCount(linked);
			if(available)
				attributes.Add(*linked, -available);
		}
		return attributes.CanAdd(*outfit, -1);
	}

	// Ammo is not a factor, so check whether this ship can uninstall this outfit by itself.
	return ship->Attributes().CanAdd(*outfit, -1);
}



void OutfitterPanel::DrawOutfit(const Outfit &outfit, const Point &center, bool isSelected, bool isOwned) const
{
	const Sprite *back = SpriteSet::Get(isSelected ? "ui/outfitter selected" : "ui/outfitter unselected");
	SpriteShader::Draw(back, center);
	DrawThumbnail(outfit.Thumbnail(), isSelected, center);

	// Draw the outfit name.
	const string &name = outfit.DisplayName();
	const Font &font = FontSet::Get(14);
	Point offset(-.5 * OUTFIT_SIZE, -.5 * OUTFIT_SIZE + 10.);
	font.Draw({name, {OUTFIT_SIZE, Alignment::CENTER, Truncate::MIDDLE}},
		center + offset, Color((isSelected | isOwned) ? .8 : .5, 0.));
}



bool OutfitterPanel::HasLicense(const string &name) const
{
	return (IsLicense(name) && player.HasLicense(LicenseRoot(name)));
}



void OutfitterPanel::CheckRefill()
{
	if(checkedRefill)
		return;
	checkedRefill = true;
	Preferences::AmmoRefill refillPref = Preferences::GetAmmoRefill();
	if(refillPref == Preferences::AmmoRefill::NEVER)
		return;

	int count = 0;
	map<const Outfit *, int> needed;
	for(const shared_ptr<Ship> &ship : player.Ships())
	{
		// Skip ships in other systems and those that were unable to land in-system.
		if(ship->GetSystem() != player.GetSystem() || ship->IsDisabled())
			continue;

		++count;
		auto toRefill = GetRefillableAmmunition(*ship);
		for(const Outfit *outfit : toRefill)
		{
			int amount = ship->Attributes().CanAdd(*outfit, numeric_limits<int>::max());
			if(amount > 0)
			{
				bool available = outfitter.Has(outfit) || player.Stock(outfit) > 0;
				available = available || player.Cargo().Get(outfit) || player.Storage().Get(outfit);
				if(available)
					needed[outfit] += amount;
			}
		}
	}

	int64_t cost = 0;
	for(auto &it : needed)
	{
		// Don't count cost of anything installed from cargo or storage.
		it.second = max(0, it.second - player.Cargo().Get(it.first) - player.Storage().Get(it.first));
		if(!outfitter.Has(it.first))
			it.second = min(it.second, max(0, player.Stock(it.first)));
		cost += player.StockDepreciation().Value(it.first, day, it.second);
	}
	if(!needed.empty() && cost < player.Accounts().Credits())
	{
		if(refillPref == Preferences::AmmoRefill::ASK || (cost && refillPref == Preferences::AmmoRefill::WHEN_FREE))
		{
			string message = "要为你的";
			message += (count == 1) ? "飞船补充全部弹药吗？" : "各艘飞船补充全部弹药吗？";
			if(cost)
				message += "费用为 " + Format::CreditString(cost) + "。";
			GetUI().Push(DialogPanel::CallFunctionIfOk(this, &OutfitterPanel::Refill, message));
		}
		else
			Refill();
	}
}



void OutfitterPanel::Refill()
{
	for(const shared_ptr<Ship> &ship : player.Ships())
	{
		// Skip ships in other systems and those that were unable to land in-system.
		if(ship->GetSystem() != player.GetSystem() || ship->IsDisabled())
			continue;

		auto toRefill = GetRefillableAmmunition(*ship);
		for(const Outfit *outfit : toRefill)
		{
			int neededAmmo = ship->Attributes().CanAdd(*outfit, numeric_limits<int>::max());
			if(neededAmmo > 0)
			{
				// Fill first from any stockpiles in storage.
				const int fromStorage = player.Storage().Remove(outfit, neededAmmo);
				neededAmmo -= fromStorage;
				// Then from cargo.
				const int fromCargo = player.Cargo().Remove(outfit, neededAmmo);
				neededAmmo -= fromCargo;
				// Then, buy at reduced (or full) price.
				int available = outfitter.Has(outfit) ? neededAmmo : min<int>(neededAmmo, max<int>(0, player.Stock(outfit)));
				if(neededAmmo && available > 0)
				{
					int64_t price = player.StockDepreciation().Value(outfit, day, available);
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
const vector<Ship *> OutfitterPanel::GetShipsToOutfit(bool isInstall) const
{
	vector<Ship *> shipsToOutfit;
	int compareValue = isInstall ? numeric_limits<int>::max() : 0;
	int compareMod = 2 * isInstall - 1;
	for(Ship *ship : playerShips)
	{
		if((isInstall && !ShipCanAdd(ship, selectedOutfit))
				|| (!isInstall && !ShipCanRemove(ship, selectedOutfit)))
			continue;

		int count = ship->OutfitCount(selectedOutfit);
		if(compareMod * count < compareMod * compareValue)
		{
			shipsToOutfit.clear();
			compareValue = count;
		}
		if(count == compareValue)
			shipsToOutfit.push_back(ship);
	}

	return shipsToOutfit;
}



int OutfitterPanel::FindItem(const string &text) const
{
	int bestIndex = 9999;
	int bestItem = -1;
	auto it = zones.begin();
	for(unsigned int i = 0; i < zones.size(); ++i, ++it)
	{
		const Outfit *outfit = it->GetOutfit();
		int index = Format::Search(outfit->DisplayName(), text);
		if(index >= 0 && index < bestIndex)
		{
			bestIndex = index;
			bestItem = i;
			if(!index)
				return i;
		}
	}
	return bestItem;
}



double OutfitterPanel::ButtonPanelHeight() const
{
	// The 70 = (3 x 10 (pad) + 20 x 2 (text)) for the credit and cargo space information lines.
	return 70. + BUTTON_HEIGHT * 3 + BUTTON_ROW_PAD * 2;
}



void OutfitterPanel::DrawButtons()
{
	// There will be three rows of buttons:
	//  [ Buy  ] [  Install  ] [  Cargo  ]
	//  [ Sell ] [ Uninstall ] [ Storage ]
	//   Quantity [Dropdown]   [  Leave  ]
	const double rowOffsetY = BUTTON_HEIGHT + BUTTON_ROW_PAD;
	const double rowBaseY = Screen::BottomRight().Y() - 2 * rowOffsetY - .5 * BUTTON_HEIGHT - BUTTON_ROW_START_PAD;
	const double buttonOffsetX = BUTTON_WIDTH + BUTTON_COL_PAD;
	const double buttonCenterX = Screen::Right() - SIDEBAR_WIDTH / 2 - 1.;
	const Point buttonSize{BUTTON_WIDTH, BUTTON_HEIGHT};

	// Draw the button panel (shop side panel footer).
	const Point buttonPanelSize(SIDEBAR_WIDTH, ButtonPanelHeight());
	const Rectangle buttonsFooter(Screen::BottomRight() - .5 * buttonPanelSize, buttonPanelSize);
	FillShader::Fill(buttonsFooter, *GameData::Colors().Get("shop side panel background"));
	FillShader::Fill(
		Point(Screen::Right() - SIDEBAR_WIDTH / 2, Screen::Bottom() - ButtonPanelHeight()),
		Point(SIDEBAR_WIDTH, 1), *GameData::Colors().Get("shop side panel footer"));

	// Set up font size and colors for the credits.
	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &dim = *GameData::Colors().Get("medium");

	// Draw the row for credits display.
	const Point creditsPoint(
		Screen::Right() - SIDEBAR_WIDTH + 10,
		Screen::Bottom() - ButtonPanelHeight() + 10);
	font.Draw("You have:", creditsPoint, dim);
	const string credits = Format::CreditString(player.Accounts().Credits());
	font.Draw({credits, {SIDEBAR_WIDTH - 20, Alignment::RIGHT}}, creditsPoint, bright);

	// Draw the row for Fleet Cargo Space free.
	const Point cargoPoint(
		Screen::Right() - SIDEBAR_WIDTH + 10,
		Screen::Bottom() - ButtonPanelHeight() + 30);
	font.Draw("Cargo Free:", cargoPoint, dim);
	string space = Format::Number(player.Cargo().Free()) + " / " + Format::Number(player.Cargo().Size());
	font.Draw({space, {SIDEBAR_WIDTH - 20, Alignment::RIGHT}}, cargoPoint, bright);

	// Clear the buttonZones, they will be populated again as buttons are drawn.
	buttonZones.clear();

	// Row 1.
	DrawButton("_Buy", Rectangle(Point(buttonCenterX + buttonOffsetX * -1, rowBaseY + rowOffsetY * 0), buttonSize),
		ButtonActive('b'), hoverButton == 'b', 'b');
	DrawButton("_Install", Rectangle(Point(buttonCenterX + buttonOffsetX * 0, rowBaseY + rowOffsetY * 0), buttonSize),
		ButtonActive('i'), hoverButton == 'i', 'i');
	DrawButton("_Cargo", Rectangle(Point(buttonCenterX + buttonOffsetX * 1, rowBaseY + rowOffsetY * 0), buttonSize),
		ButtonActive('c'), hoverButton == 'c', 'c');
	// Row 2.
	DrawButton("_Sell", Rectangle(Point(buttonCenterX + buttonOffsetX * -1, rowBaseY + rowOffsetY * 1), buttonSize),
		ButtonActive('s'), hoverButton == 's', 's');
	DrawButton(!CanMoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage) &&
		CanMoveOutfit(OutfitLocation::Cargo, OutfitLocation::Storage) ? "_Unload" : "_Uninstall",
		Rectangle(Point(buttonCenterX + buttonOffsetX * 0, rowBaseY + rowOffsetY * 1), buttonSize),
		ButtonActive('u'), hoverButton == 'u', 'u');
	DrawButton("Sto_re", Rectangle(Point(buttonCenterX + buttonOffsetX * 1, rowBaseY + rowOffsetY * 1), buttonSize),
		ButtonActive('r'), hoverButton == 'r', 'r');
	// Row 3.
	DrawButton("_Leave", Rectangle(Point(buttonCenterX + buttonOffsetX * 1, rowBaseY + rowOffsetY * 2), buttonSize),
		true, hoverButton == 'l', 'l');

	font.Draw("Quantity:", Screen::BottomRight() - Point(SIDEBAR_WIDTH - 10, 33), dim);

	const Point sqCenter = Screen::BottomRight() - Point(135, 25);
	selectedQuantity->SetPosition(Rectangle(sqCenter, {86, 20}));

	// Draw tooltips for the button being hovered over:
	string tooltip = GameData::Tooltip(string("outfitter: ") + hoverButton);
	if(!tooltip.empty())
	{
		buttonsTooltip.IncrementCount();
		if(buttonsTooltip.ShouldDraw())
		{
			buttonsTooltip.SetZone(buttonsFooter);
			buttonsTooltip.SetText(tooltip);
			buttonsTooltip.Draw();
		}
	}
	else
		buttonsTooltip.DecrementCount();

	// Draw the tooltip for your full number of credits and free cargo space
	const Rectangle creditsBox = Rectangle::FromCorner(creditsPoint, Point(SIDEBAR_WIDTH - 20, 30));
	if(creditsBox.Contains(hoverPoint))
	{
		creditsTooltip.IncrementCount();
		if(creditsTooltip.ShouldDraw())
		{
			creditsTooltip.SetZone(creditsBox);
			creditsTooltip.SetText(Format::CreditString(player.Accounts().Credits(), false) + '\n' +
				"空闲 " + Format::MassString(player.Cargo().Free()) + "，总容量 " +
				Format::MassString(player.Cargo().Size()));
			creditsTooltip.Draw();
		}
	}
	else
		creditsTooltip.DecrementCount();
}



ShopPanel::TransactionResult OutfitterPanel::HandleShortcuts(SDL_Keycode key)
{
	TransactionResult result = false;
	if(key == 'b')
	{
		// Buy and install up to <modifier> outfits for each selected ship.
		result = MoveOutfit(OutfitLocation::Shop, OutfitLocation::Ship, "购买并安装");
	}
	else if(key == 's')
	{
		// Sell <modifier> of the selected outfit from cargo, or else storage, or else from each selected ship.
		// Return a result based on the reason that none can be sold from the selected ships.
		if(!MoveOutfit(OutfitLocation::Cargo, OutfitLocation::Shop)
				&& !MoveOutfit(OutfitLocation::Storage, OutfitLocation::Shop))
			result = MoveOutfit(OutfitLocation::Ship, OutfitLocation::Shop, "出售");
	}
	else if(key == 'r')
	{
		// Move <modifier> of the selected outfit to storage from either cargo, the selected ships, or from the shop.
		if(!MoveOutfit(OutfitLocation::Cargo, OutfitLocation::Storage)
				&& !MoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage))
			result = MoveOutfit(OutfitLocation::Shop, OutfitLocation::Storage, "存储");
	}
	else if(key == 'c')
	{
		// Either move up to <modifier> outfits into cargo from storage if any are in storage, or else buy up to
		// <modifier> outfits into cargo.
		// Note: If the outfit cannot be moved from storage or bought into cargo, give an error based on the buy
		// condition.
		if(!MoveOutfit(OutfitLocation::Storage, OutfitLocation::Cargo))
			result = MoveOutfit(OutfitLocation::Shop, OutfitLocation::Cargo, "购买并装入货舱");
	}
	else if(key == 'i')
	{
		// Install up to <modifier> outfits from already owned equipment into each selected ship.
		result = MoveOutfit(OutfitLocation::Cargo, OutfitLocation::Ship, "安装");
		if(!result && !result.canSource)
			result = MoveOutfit(OutfitLocation::Storage, OutfitLocation::Ship, "安装");
	}
	else if(key == 'u')
	{
		// Uninstall up to <modifier> outfits from each of the selected ships if any are available to uninstall,
		// moving the outfits into cargo if there is space, or to storage if there isn't.
		result = MoveOutfit(OutfitLocation::Ship, OutfitLocation::Cargo, "卸载");
		if(!result && !result.canPlace)
			result = MoveOutfit(OutfitLocation::Ship, OutfitLocation::Storage, "卸载");
	}

	return result;
}
