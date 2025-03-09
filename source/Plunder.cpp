/* Plunder.cpp
Extracted from BoardingPanel.cpp, copyright (c) 2014 by Michael Zahniser.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Plunder.h"

#include "CargoHold.h"
#include "Depreciation.h"
#include "text/Format.h"
#include "Outfit.h"
#include "Ship.h"
#include "System.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace std;

// Constructor (commodity cargo).
Plunder::Plunder(const string &commodity, int count, int unitValue)
	: name(commodity), outfit(nullptr), count(count), unitValue(unitValue)
{
	UpdateStrings();
}



// Constructor (outfit installed in the target ship or transported as cargo).
Plunder::Plunder(const Outfit *outfit, int count)
	: name(outfit->DisplayName()), outfit(outfit), count(count),
	unitValue(outfit->Cost() * (outfit->Get("installable") < 0. ? 1 : Depreciation::Full()))
{
	UpdateStrings();
}



// Sort by value per ton of mass.
bool Plunder::operator<(const Plunder &other) const
{
	// This may involve infinite values when the mass is zero, but that's okay.
	return (unitValue / UnitMass() > other.unitValue / other.UnitMass());
}



// Check how many of this item are left un-plundered. Once this is zero,
// the item can be removed from the list.
int Plunder::Count() const
{
	return count;
}



// Get the value of each unit of this plunder item.
int64_t Plunder::UnitValue() const
{
	return unitValue;
}



// Get the name of this item. If it is a commodity, this is its name.
const string &Plunder::Name() const
{
	return name;
}



// Get the mass, in the format "<count> x <unit mass>". If the count is
// 1, only the unit mass is reported.
const string &Plunder::Size() const
{
	return size;
}



// Get the total value (unit value times count) as a string.
const string &Plunder::Value() const
{
	return value;
}



// If this is an outfit, get the outfit. Otherwise, this returns null.
const Outfit *Plunder::GetOutfit() const
{
	return outfit;
}



/**
 * Check if the ship has enough space to take this plunder item.
 *
 * @param ship The ship that is considering taking the plunder.
 *
 * @return Whether or not the ship can take at least one of this item.
 */
bool Plunder::HasEnoughSpace(const shared_ptr<Ship> &ship) const
{
	// If there's cargo space for this outfit, the ship can take it.
	double mass = UnitMass();
	if(ship->Cargo().Free() >= mass)
		return true;

	// Otherwise, check if it is ammo for any of of the ship's weapons,
	// and if there is enough space to install it as an outfit.
	if(outfit)
		for(const auto &it : ship->Outfits())
			if(it.first != outfit && it.first->Ammo() == outfit && ship->Attributes().CanAdd(*outfit))
				return true;

	return false;
}



/**
 * Some outfits are marked as "unplunderable" in the outfit data.
 * This means that the outfit is kept in a secure location and cannot be
 * plundered until the ship has been conquered. It is used for things
 * like the ship's power systems, hyperdrive, shield generators,
 * hand-to-hand weapons, bunk rooms, and expansions.
 *
 * From a game design perspective, this locks some of the most rare and
 * valuable items behind a requirement to conquer the disabled ship.
 * This presents an ongoing risk/reward scenario for the player, since
 * they might lose crew in the process of conquering the ship.
 *
 * It also makes it far more difficult for players to obtain alien
 * technology in the early game by hunting for disabled ships in
 * contested systems. They can still get things like weapons and
 * engines, but they can't obtain things like jump drives, system cores,
 * and high-value reactors until they have a fleet capable of invading
 * and conquering those ships.
 *
 * Since advanced ships with valuable outfits are often better defended
 * against boarders, capturing those outfits is far more expensive due
 * to the casualties that the player will incur in the process.
 *
 * Additionally, some outfits are used to defend the ship from invaders,
 * and removing them would make the ship unfairly easy to conquer.
 *
 * @return True if the owner must be conquered before plundering, false otherwise.
 */
bool Plunder::RequiresConquest() const
{
  return outfit && outfit->Get("unplunerable") > 0.0;
}



/**
 * Update the count of a Plunder item.
 *
 * Also updates the item's descriptive strings.
 *
 * @param amount The amount to add or subtract from the current count.
 *
 * @return The new count.
 */
int Plunder::UpdateCount(int amount)
{
	count -= amount;
	UpdateStrings();
	return count;
}



// Update the text to reflect a change in the item count.
void Plunder::UpdateStrings()
{
	double mass = UnitMass();
	if(count == 1)
		size = Format::Number(mass);
	else
		size = to_string(count) + " x " + Format::Number(mass);

	value = Format::Credits(unitValue * count);
}



// Commodities come in units of one ton.
double Plunder::UnitMass() const
{
	return outfit ? outfit->Mass() : 1.;
}



// Functions for the Plunder::Session class.

/**
 * Constructor for the Plunder::Session class.
 *
 * @param target The ship from which to plunder.
 * @param attacker The ship that is plundering.
 */
Plunder::Session::Session(shared_ptr<Ship> &target, shared_ptr<Ship> &attacker, const vector<shared_ptr<Ship>> &attackerFleet) :
	attacker(attacker),
	attackerFleet(attackerFleet),
	target(target),
	remaining(BuildPlunderList(target)),
	taken({}),
	totalCommodityMassTaken(0),
	totalMassTaken(0),
	totalOutfitsTaken(0),
	totalValueTaken(0)
{
}



/**
 * Get the list of items that can be plundered from the target ship.
 * This list is sorted by value per ton of mass, with the most valuable
 * items first.
 *
 * @return A vector of Plunder objects representing the items that can be plundered.
 */
const vector<shared_ptr<Plunder>> &Plunder::Session::RemainingPlunder() const
{
	return remaining;
}



/**
 * TODO: Make this part of BoardingCombat.
 *
 * Take as much valuable plunder as possible from the target ship.
 * This function performs the entire plundering process in one go, such
 * as when one AI-controlled ship plunders another.
 *
 * Iterates through the list of plunder from most to least valuable per
 * mass, and takes as much as possible from each item. Stops when the
 * attacker's cargo hold is full or there is no more plunder to take.
 */
// void Plunder::Session::Raid()
// {
// 	for(size_t i = 0; i < plunder.size(); ++i)
// 	{
// 		Take(i);

// 		if(attacker->Cargo().Free() < 1)
// 			break;
// 	}

// 	for(size_t i = 0; i < plunder.size(); ++i)
// 	{
// 		if(plunder[i].Count() < 1)
// 			plunder.erase(plunder.begin() + i--);
// 	}
// }



/**
 * Take the specified number of a given item as possible from the list of plunder.
 * If the item is a commodity, as many as possible will be taken, ignoring quantity.
 * If the item is an outfit, the specified quantity will be taken if possible.
 * When a quantity is not specified, as many as possible will be taken.
 *
 * Returns how many were successfully taken.
 *
 * @param index The index of the item to take.
 * @param number The number of items to take.
 * @param pruneList Whether to remove the item from the list if all of it
 * 	was taken. If false, the item remains in the list with a count of zero.
 * 	Leave this false if you want to iterate through the list and take several
 * 	items of various types, as that will prevent the list from shifting as you
 * 	remove items from it.
 *
 * @return The number of items taken.
 */
int Plunder::Session::Take(int index, bool pruneList, int quantity)
{
	Plunder item = *remaining.at(index);
	int available = item.Count();
	if(quantity < 0 || available < quantity)
		quantity = available;
	int takenCount = 0;

	const Outfit *outfit = item.GetOutfit();
	if(outfit)
	{
		// Check if this outfit is ammo for one of the attacker's weapons.
		// If so, use it to refill the their ammo rather than putting it in cargo.
		for(const auto &it : attacker->Outfits())
			if(it.first != outfit && it.first->Ammo() == outfit)
			{
				// Figure out how many of these outfits the attacker can install.
				takenCount = attacker->Attributes().CanAdd(*outfit, quantity);
				attacker->AddOutfit(outfit, takenCount);
				// They have now installed as many of these items as possible.
				break;
			}

		// Transfer the remainder of the requested quantity to the attacker's cargo hold.
		if(takenCount < quantity)
			takenCount += attacker->Cargo().Add(outfit, quantity - takenCount);

		// Take outfits from cargo first, then from the ship itself.
		int fromCargo = target->Cargo().Remove(outfit, takenCount);
		// There isn't a RemoveOutfit function, so we call AddOutfit with a negative.
		target->AddOutfit(outfit, fromCargo - takenCount);

		// Update the total number of outfits that have been taken so far.
		totalOutfitsTaken += takenCount;
	}
	else // This is a commodity.
	{
		takenCount = target->Cargo().Transfer(item.Name(), available, attacker->Cargo());

		// Update the total mass of commodities that have been taken so far.
		totalCommodityMassTaken += takenCount * item.UnitMass();
	}

	// If pruneList is true and all of the plunder of this type was taken,
	// remove it from the list. Otherwise, just update the item's count.
	if(pruneList && takenCount == available)
	{
		remaining.erase(remaining.begin() + index);
		index = min<int>(index, remaining.size());
	}
	else
		remaining.at(index)->UpdateCount(-takenCount);

	// Update the total mass of the plunder that has been taken so far.
	totalMassTaken += takenCount * item.UnitMass();

	// Update the total value of the plunder that has been taken so far.
	totalValueTaken += takenCount * item.UnitValue();

	// Add the taken items to the list of plunder that has been taken so far.
	auto it = find_if(taken.begin(), taken.end(), [&](auto &p) {
		return p->Name() == item.Name();
	});
	if(it == taken.end())
		taken.push_back(make_shared<Plunder>(item.Name(), takenCount, item.UnitValue()));
	else
		(*it)->UpdateCount(takenCount);

	return takenCount;
}



/**
 * Get a message describing the result of the plunder session.
 * This message is intended to be displayed to the player after one of
 * their escorts has plundered a disabled ship or been plundered in turn.
 *
 * @return A string describing the result of the plunder session.
 */
const std::string Plunder::Session::GetSummary() const
{
  std::string message = "\"" + attacker->Name() + "\" plundered ";

	if(totalOutfitsTaken)
		message += Format::Number(totalOutfitsTaken) + " outfits";

	if(totalOutfitsTaken && totalCommodityMassTaken)
		message += " and ";

	if(totalCommodityMassTaken)
		message += Format::CargoString(totalCommodityMassTaken, "commodities");

	message += " from \"" + target->Name()
		+ "\" for a total value of " + Format::Credits(totalValueTaken)
		+ " credits";

	if(!target->IsYours())
	{
		message += " (" + Format::CargoString(attacker->Cargo().Free(), "free space") + " remaining";

		if(attackerFleet.size() > 1)
		{
			int total = 0;
			for(const shared_ptr<Ship> &ship : attackerFleet)
				if(!ship->IsDestroyed() && !ship->IsParked() && ship->GetSystem() == attacker->GetSystem())
					total += ship->Cargo().Free();

			message += "; " + Format::MassString(total) + " in fleet";
		}
		message += ").";
	}

	if(remaining.empty())
		message += " \"" + target->Name() + "\" has nothing left that can be plundered.";

	return message;
}



/**
 * Get a list of all of the plunder that was taken.
 *
 * @return A vector of Plunder objects representing the items that were taken.
 *
 */
const std::vector<shared_ptr<Plunder>> &Plunder::Session::TakenPlunder() const
{
	return taken;
}



/**
 * Get the total mass of commodities that were taken as plunder.
 *
 * @return The total mass of the commodities that were taken.
 */
int64_t Plunder::Session::TotalCommodityMassTaken() const
{
  return totalCommodityMassTaken;
}



/**
 * Get the total mass of items that were taken as plunder.
 *
 * @return The total mass of the plunder that was taken.
 */
int64_t Plunder::Session::TotalMassTaken() const
{
	return totalMassTaken;
}



/**
 * Get the total number of outfits that were taken as plunder.
 *
 * @return The total number of outfits that were taken.
 */
int64_t Plunder::Session::TotalOutfitsTaken() const
{
  return totalOutfitsTaken;
}



/**
 * Get the total value of all the plunder that was taken.
 *
 * @return The total value of all the plunder that was taken.
 */
int64_t Plunder::Session::TotalValueTaken() const
{
	return totalValueTaken;
}



/**
 * Build a list of items that can be plundered from the target ship.
 * This list is sorted by value per ton of mass, with the most valuable
 * items first.
 *
 * @param ship The ship from which to plunder.
 * @return A vector of Plunder objects representing the items that can be plundered.
 */
vector<shared_ptr<Plunder>> Plunder::Session::BuildPlunderList(const shared_ptr<Ship> &ship)
{
	vector<shared_ptr<Plunder>> plunder;

	// Add all the commodities that the ship is carrying.
	for(const auto &it : ship->Cargo().Commodities())
		if(it.second)
			plunder.push_back(make_shared<Plunder>(it.first, it.second, ship->GetSystem()->Trade(it.first)));

	// Add all the outfits that can be plundered from the ship.
	auto outfits = ship->PlunderableOutfits();
	for(const auto &it : *outfits)
		if(it.second)
			plunder.push_back(make_shared<Plunder>(it.first, it.second));

	// Sort by value per ton of mass.
	sort(plunder.begin(), plunder.end());

	return plunder;
}
