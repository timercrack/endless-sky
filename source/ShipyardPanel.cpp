/* ShipyardPanel.cpp
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

#include "ShipyardPanel.h"

#include "text/Alignment.h"
#include "comparators/BySeriesAndIndex.h"
#include "ClickZone.h"
#include "DialogPanel.h"
#include "text/DisplayText.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "GameData.h"
#include "Gamerules.h"
#include "Government.h"
#include "Mission.h"
#include "Phrase.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Point.h"
#include "Screen.h"
#include "Ship.h"
#include "ShipNameDialogPanel.h"
#include "image/Sprite.h"
#include "image/SpriteSet.h"
#include "shader/SpriteShader.h"
#include "text/Truncate.h"
#include "UI.h"

#include <algorithm>
#include <utility>

class System;

using namespace std;

namespace {
	const int MAX_LIST = 20;
}



ShipyardPanel::ShipyardPanel(PlayerInfo &player, const Sale<Ship> &stock)
	: ShopPanel(player, false), modifier(0), shipyard(stock), initialFleetUsage(player.FleetCost())
{
	for(const auto &it : GameData::Ships())
		catalog[it.second.Attributes().Category()].push_back(it.first);

	for(pair<const string, vector<string>> &it : catalog)
		sort(it.second.begin(), it.second.end(), BySeriesAndIndex<Ship>());
}



void ShipyardPanel::Step()
{
	ShopPanel::Step();
	ShopPanel::CheckForMissions(Mission::SHIPYARD);
	ShopPanel::ValidateSelectedShips();
	if(!GetUI().IsTop(this) || DoHelp("shipyard") || hasDoneFleetCapacityWarning)
		return;

	int capacity = player.FleetCapacity();
	if(initialFleetUsage <= capacity && player.FleetCost() > capacity)
	{
		hasDoneFleetCapacityWarning = true;
		bool shipCap = GameData::GetGamerules().GetFleetSizeLimitation() == Gamerules::FleetSizeLimitation::SHIP_CAP;
		GetUI().Push(DialogPanel::Info("你当前出动的护航舰已超出舰队容量上限。"
			"除非停泊或出售部分护航舰以腾出空间"s +
			(shipCap ? "" : "，或将旗舰更换为占用上限更多的飞船（旗舰本身不计入舰队容量）")
			+ "，否则你将无法从这颗星球起飞。"));
	}
}



int ShipyardPanel::TileSize() const
{
	return SHIP_SIZE;
}



bool ShipyardPanel::HasItem(const string &name) const
{
	const Ship *ship = GameData::Ships().Get(name);
	return shipyard.Has(ship);
}



void ShipyardPanel::DrawItem(const string &name, const Point &point)
{
	const Ship *ship = GameData::Ships().Get(name);
	zones.emplace_back(point, Point(SHIP_SIZE, SHIP_SIZE), ship);
	if(point.Y() + SHIP_SIZE / 2 < Screen::Top() || point.Y() - SHIP_SIZE / 2 > Screen::Bottom())
		return;

	DrawShip(*ship, point, ship == selectedShip);
}



double ShipyardPanel::ButtonPanelHeight() const
{
	// The 50 = (3 x 10 (pad) + 20 x 1 (text)) for the credit information line.
	// Add 20 to make room for the fleet capacity line if that gamerule is active.
	return 50 + BUTTON_HEIGHT * 3 + BUTTON_ROW_PAD * 2 + 20 * hasFleetCapacity;
}



double ShipyardPanel::DrawDetails(const Point &center)
{
	string selectedItem = "No Ship Selected";
	const Font &font = FontSet::Get(14);

	double heightOffset = 20.;

	if(selectedShip)
	{
		selectedItem = selectedShip->DisplayModelName();

		const Point spriteCenter(center.X(), center.Y() + 20 + TileSize() / 2);
		const Point startPoint(center.X() - INFOBAR_WIDTH / 2 + 20, center.Y() + 20 + TileSize());
		const Sprite *background = SpriteSet::Get("ui/shipyard selected");
		SpriteShader::Draw(background, spriteCenter);

		const Sprite *shipSprite = selectedShip->GetSprite();
		if(shipSprite)
		{
			const float spriteScale = min(1.f, (INFOBAR_WIDTH - 60.f) / max(shipSprite->Width(), shipSprite->Height()));
			const Swizzle *swizzle = selectedShip->CustomSwizzle()
				? selectedShip->CustomSwizzle() : GameData::PlayerGovernment()->GetSwizzle();
			DrawThumbnail(*selectedShip, true, spriteCenter, spriteScale, swizzle);
		}

		heightOffset = startPoint.Y() + DrawShipInfo(startPoint, *selectedShip, true);
	}

	// Draw this string representing the selected ship (if any), centered in the details side panel.
	const Color &bright = *GameData::Colors().Get("bright");
	const Point selectedPoint(center.X() - INFOBAR_WIDTH / 2, center.Y());
	font.Draw({selectedItem, {INFOBAR_WIDTH, Alignment::CENTER, Truncate::MIDDLE}},
		selectedPoint, bright);

	return heightOffset;
}



void ShipyardPanel::DrawButtons()
{
	// There will be three rows of buttons:
	//  [    Buy    ] [    Sell    ] [   Rewire  ]
	//                [  Sell Hull ]
	//    Quantity [Dropdown]        [   Leave   ]
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

	// Draw the row for your fleet capacity.
	if(hasFleetCapacity)
	{
		const Point fleetCapPoint(
			Screen::Right() - SIDEBAR_WIDTH + 10,
			Screen::Bottom() - ButtonPanelHeight() + 30);
		font.Draw("Fleet Capacity:", fleetCapPoint, dim);
		string limit = Format::AbbreviatedNumber(player.FleetCost()) + " / "
			+ Format::AbbreviatedNumber(player.FleetCapacity());
		font.Draw({limit, {SIDEBAR_WIDTH - 20, Alignment::RIGHT}}, fleetCapPoint, bright);
	}

	// Clear the buttonZones, they will be populated again as buttons are drawn.
	buttonZones.clear();

	// Row 1:
	DrawButton("_Buy",
		Rectangle(Point(buttonCenterX + buttonOffsetX * -1, rowBaseY + rowOffsetY * 0), buttonSize),
		static_cast<bool>(CanDoBuyButton()), hoverButton == 'b', 'b');
	DrawButton("_Sell",
		Rectangle(Point(buttonCenterX + buttonOffsetX * 0, rowBaseY + rowOffsetY * 0), buttonSize),
		static_cast<bool>(playerShips.size()), hoverButton == 's', 's');
	DrawButton("_Rewire",
		Rectangle(Point(buttonCenterX + buttonOffsetX * 1, rowBaseY + rowOffsetY * 0), buttonSize),
		static_cast<bool>(CanRewire()), hoverButton == 'r', 'r');
	// Row 2:
	DrawButton("Sell H_ull",
		Rectangle(Point(buttonCenterX + buttonOffsetX * 0, rowBaseY + rowOffsetY * 1), buttonSize),
		static_cast<bool>(playerShips.size()), hoverButton == 'u', 'u');
	// Row 3:
	DrawButton("_Leave",
		Rectangle(Point(buttonCenterX + buttonOffsetX * 1, rowBaseY + rowOffsetY * 2), buttonSize),
		true, hoverButton == 'l', 'l');

	font.Draw("Quantity:", Screen::BottomRight() - Point(SIDEBAR_WIDTH - 10, 33), dim);

	const Point sqCenter = Screen::BottomRight() - Point(135, 25);
	selectedQuantity->SetPosition(Rectangle(sqCenter, {86, 20}));

	// Draw tooltips for the button being hovered over:
	string tooltip = GameData::Tooltip(string("shipyard: ") + hoverButton);
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

	// Draw the tooltip for your full number of credits.
	const Rectangle creditsBox = Rectangle::FromCorner(creditsPoint,
		Point(SIDEBAR_WIDTH - 20, 15 + 20 * hasFleetCapacity));
	if(creditsBox.Contains(hoverPoint))
	{
		creditsTooltip.IncrementCount();
		if(creditsTooltip.ShouldDraw())
		{
			creditsTooltip.SetZone(creditsBox);
			string tooltipText = Format::CreditString(player.Accounts().Credits(), false);
			if(hasFleetCapacity)
				tooltipText += "\n已用舰队容量：" + Format::Number(player.FleetCost()) + " / "
					+ Format::Number(player.FleetCapacity());
			creditsTooltip.SetText(tooltipText);
			creditsTooltip.Draw();
		}
	}
	else
		creditsTooltip.DecrementCount();
}



ShopPanel::TransactionResult ShipyardPanel::HandleShortcuts(SDL_Keycode key)
{
	TransactionResult result = false;
	if(key == 'b')
	{
		// Buy up to <modifier> ships.
		result = CanDoBuyButton();
		if(result)
			DoBuyButton();
	}
	else if(key == 's')
	{
		// Sell selected ships and outfits.
		if(playerShip)
			Sell(false);
	}
	else if(key == 'u')
	{
		// Sell selected ships and move outfits to Storage.
		if(playerShip)
			Sell(true);
	}
	else if(key == 'r')
	{
		// Repair selected ships if able.
		result = CanRewire();
		if(result)
			Rewire();
	}

	return result;
}



ShopPanel::TransactionResult ShipyardPanel::CanDoBuyButton() const
{
	if(!selectedShip)
		return false;

	int64_t cost = player.StockDepreciation().Value(*selectedShip, day);

	// Check that the player has any necessary licenses.
	int64_t licenseCost = LicenseCost(&selectedShip->Attributes());
	if(licenseCost < 0)
		return "购买这艘飞船需要特殊许可证。"
			"你可能需要完成某些任务才能获得。";

	// Check if the player can't pay.
	cost += licenseCost;
	if(player.Accounts().Credits() < cost)
	{
		// Check if ships could be sold to pay for the new ship.
		for(const auto &it : player.Ships())
			cost -= player.FleetDepreciation().Value(*it, day);

		if(player.Accounts().Credits() >= cost)
		{
			string ship = (player.Ships().size() == 1) ? "你当前的飞船" : "你的部分飞船";
			return "你没有足够的信用点购买这艘飞船。"
				"如果想买下它，必须先出售" + ship + "。";
		}

		// Check if the license cost is the tipping point.
		if(player.Accounts().Credits() >= cost - licenseCost)
			return "你没有足够的信用点购买这艘飞船，"
				"因为购买必需的许可证还需额外花费 " + Format::AbbreviatedNumber(licenseCost) +
				" 信用点。"
				"不妨看看银行是否愿意给你贷款。";

		return "你没有足够的信用点购买这艘飞船。"
				"不妨看看银行是否愿意给你贷款。";
	}
	return true;
}



void ShipyardPanel::DoBuyButton()
{
	int64_t licenseCost = LicenseCost(&selectedShip->Attributes());
	if(licenseCost < 0)
		return;

	const string &quantity = selectedQuantity->Text();
	modifier = quantity.empty() ? 1 : stoi(quantity);
	string message;
	if(licenseCost)
		message = "注意：除飞船本身的价格外，你还需支付 " + Format::CreditString(licenseCost)
			+ " 购买驾驶这艘飞船所需的许可证。"
			"如果没有问题，请为你全新的";
	else
		message = "请为你全新的";

	if(modifier == 1)
		message += selectedShip->DisplayModelName() + "命名！（留空则随机选取名称。）";
	else
		message += selectedShip->PluralModelName() + "命名！（留空则随机选取名称。）";

	GetUI().Push(ShipNameDialogPanel::Create(
			DialogPanel::FunctionButton(this, "购买", 'b', &ShipyardPanel::BuyShip),
			message));
}



void ShipyardPanel::Sell(bool storeOutfits)
{
	int count = playerShips.size();
	int initialCount = count;
	string message;

	if(storeOutfits && !planet->HasOutfitter())
	{
		message = "警告：这颗星球没有装备商，"
			"无法将装备保留在存储中。\n";
		storeOutfits = false;
	}
	// Never allow keeping outfits where they cannot be retrieved.
	// TODO: Consider how to keep outfits in Cargo in the future.

	if(!storeOutfits)
		message += "出售 ";
	else
		message = "仅出售船体：";
	if(count == 1)
		message += playerShip->GivenName();
	else if(count <= MAX_LIST)
	{
		auto it = playerShips.begin();
		message += (*it++)->GivenName();
		--count;

		if(count == 1)
			message += " 和 ";
		else
		{
			while(count-- > 1)
				message += "，\n" + (*it++)->GivenName();
			message += "，\n以及 ";
		}
		message += (*it)->GivenName();
	}
	else
	{
		auto it = playerShips.begin();
		message += (*it++)->GivenName() + "，\n";
		for(int i = 1; i < MAX_LIST - 1; ++i)
			message += (*it++)->GivenName() + "，\n";

		message += "以及其他 " + to_string(count - (MAX_LIST - 1)) + " 艘飞船";
	}
	// To allow calculating the sale price of all the ships in the list,
	// temporarily copy into a shared_ptr vector:
	vector<shared_ptr<Ship>> toSell;
	for(const auto &it : playerShips)
		toSell.push_back(it->shared_from_this());
	int64_t total = player.FleetDepreciation().Value(toSell, day, storeOutfits);

	message += ((initialCount > 2) ? "\n售价 " : "，售价 ") + Format::CreditString(total) + "？";

	if(storeOutfits)
	{
		message += "所有装备将被放入存储。";
		GetUI().Push(DialogPanel::CallFunctionIfOk(this, &ShipyardPanel::SellShipChassis, message, Truncate::MIDDLE));
	}
	else
		GetUI().Push(DialogPanel::CallFunctionIfOk(this, &ShipyardPanel::SellShipAndOutfits, message, Truncate::MIDDLE));
}



ShopPanel::TransactionResult ShipyardPanel::CanRewire()
{
	shipsToRewire.clear();

	if(!playerShip)
		return "未选择飞船。";

	bool cannotUnlock = false;
	for(Ship *selected : playerShips)
	{
		if(!selected->IsLocked())
			continue;
		if(selected->CannotUnlock())
		{
			cannotUnlock = true;
			continue;
		}
		shipsToRewire.insert(selected);
	}

	if(shipsToRewire.empty())
	{
		bool singleSelection = playerShips.size() == 1;
		if(cannotUnlock)
			return singleSelection ? "选中的飞船无法重新布线。" : "至少有一艘选中的飞船无法重新布线。";
		return singleSelection ? "选中的飞船无需重新布线。" : "选中的飞船均无需重新布线。";
	}

	int64_t cost = 0;
	int64_t minimum = 0;
	for(const auto &it : shipsToRewire)
	{
		cost += it->RewiringCost();
		minimum = min(minimum, it->RewiringCost());
	}

	int64_t credits = player.Accounts().Credits();
	if(credits < cost)
	{
		if(shipsToRewire.size() == 1)
			return "你没有足够的信用点为这艘飞船重新布线。"
					"不妨看看银行是否愿意给你贷款。";

		// Alert the player if any of the selected ships could be rewired individually.
		if(credits >= minimum)
		{
			return "你没有足够的信用点同时为所有选中的飞船重新布线。"
					"如果想为其中部分飞船重新布线，请少选几艘。";
		}

		return "你没有足够的信用点为选中的飞船重新布线。"
				"不妨看看银行是否愿意给你贷款。";
	}

	return true;
}



void ShipyardPanel::Rewire()
{
	int count = shipsToRewire.size();
	string message = "要为";

	if(count == 1)
		message += (*shipsToRewire.begin())->GivenName() + "重新布线吗？";
	else
		message += "以下飞船重新布线吗？\n" + Format::IndentedList(shipsToRewire,
			[](const Ship *ship) -> string { return ship->GivenName(); },
			MAX_LIST);

	int64_t total = 0;
	for(const auto &it : shipsToRewire)
		total += it->RewiringCost();

	if(total)
		message += (count > 1 ? "\n" : "") + "费用为 "s + Format::CreditString(total) + "。";
	GetUI().Push(DialogPanel::CallFunctionIfOk(this, &ShipyardPanel::RewireShips, message, Truncate::MIDDLE));
}



bool ShipyardPanel::BuyShip(const string &name)
{
	int64_t licenseCost = LicenseCost(&selectedShip->Attributes());
	if(licenseCost)
	{
		player.Accounts().AddCredits(-licenseCost);
		for(const string &licenseName : selectedShip->Attributes().Licenses())
			if(!player.HasLicense(licenseName))
				player.AddLicense(licenseName);
	}

	for(int i = 1; i <= modifier; ++i)
	{
		// If no name is given, choose a random name. Otherwise, if buying
		// multiple ships, append a number to the given ship name.
		string shipName = name;
		if(name.empty())
			shipName = GameData::Phrases().Get("civilian")->Get();
		else if(modifier > 1)
			shipName += " " + to_string(i);

		if(!player.BuyShip(selectedShip, shipName))
			break;
	}

	playerShip = &*player.Ships().back();
	playerShips.clear();
	playerShips.insert(playerShip);
	CheckSelection();

	// Close the dialog.
	return true;
}



void ShipyardPanel::SellShipAndOutfits()
{
	SellShip(false);
}



void ShipyardPanel::SellShipChassis()
{
	SellShip(true);
}



void ShipyardPanel::SellShip(bool storeOutfits)
{
	// Sell unlocked ships before selling locked ones so that the unlocked ships
	// use the higher value depreciation records first.
	for(auto it = playerShips.begin(); it != playerShips.end(); )
	{
		Ship *ship = *it;
		if(!ship->IsLocked())
		{
			player.SellShip(ship, storeOutfits);
			it = playerShips.erase(it);
		}
		else
			++it;
	}
	for(Ship *ship : playerShips)
		player.SellShip(ship, storeOutfits);
	playerShips.clear();
	playerShip = nullptr;
	for(const shared_ptr<Ship> &ship : player.Ships())
		if(ship->GetSystem() == player.GetSystem() && !ship->IsDisabled())
		{
			playerShip = ship.get();
			break;
		}
	if(playerShip)
		playerShips.insert(playerShip);
}



void ShipyardPanel::RewireShips()
{
	for(Ship *ship : shipsToRewire)
	{
		ship->SetLock(false);
		player.Accounts().AddCredits(-ship->RewiringCost());
	}
}



int ShipyardPanel::FindItem(const string &text) const
{
	int bestIndex = 9999;
	int bestItem = -1;
	auto it = zones.begin();
	for(unsigned int i = 0; i < zones.size(); ++i, ++it)
	{
		const Ship *ship = it->GetShip();
		int index = Format::Search(ship->DisplayModelName(), text);
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
