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

using namespace std;

// Constructor (commodity cargo).
Plunder::Plunder(const string &commodity, int count, int unitValue)
	: name(commodity), outfit(nullptr), count(count), unitValue(unitValue)
{
	UpdateStrings();
}



// Constructor (outfit installed in the victim ship or transported as cargo).
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



// Find out how many of these I can take if I have this amount of cargo
// space free.
bool Plunder::CanTake(const Ship &ship) const
{
	// If there's cargo space for this outfit, you can take it.
	double mass = UnitMass();
	if(ship.Cargo().Free() >= mass)
		return true;

	// Otherwise, check if it is ammo for any of your weapons. If so, check if
	// you can install it as an outfit.
	if(outfit)
		for(const auto &it : ship.Outfits())
			if(it.first != outfit && it.first->Ammo() == outfit && ship.Attributes().CanAdd(*outfit))
				return true;

	return false;
}



// Update the count when an item is taken.
void Plunder::Remove(int count)
{
	this->count -= count;
	UpdateStrings();
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
 * @param victim The ship from which to plunder.
 * @param attacker The ship that is plundering.
 */
Plunder::Session::Session(shared_ptr<Ship> &victim, Ship * attacker) :
	attacker(attacker),
	victim(victim),
	plunder({}),
	taken({}),
	totalCommodityMassTaken(0),
	totalMassTaken(0),
	totalOutfitsTaken(0),
	totalValueTaken(0)
{
	// Add all the commodities that the victim is carrying.
	for(const auto &it : victim->Cargo().Commodities())
		if(it.second)
			plunder.emplace_back(it.first, it.second, victim->GetSystem()->Trade(it.first));

	// Add all the outfits that can be plundered from the victim.
	auto outfits = victim->PlunderableOutfits();
	for(const auto &it : *outfits)
		if(it.second)
			plunder.emplace_back(it.first, it.second);

	// Sort by value per ton of mass.
	sort(plunder.begin(), plunder.end());
}



/**
 * Get the list of items that can be plundered from the victim ship.
 * This list is sorted by value per ton of mass, with the most valuable
 * items first.
 *
 * @return A vector of Plunder objects representing the items that can be plundered.
 */
const vector<Plunder> &Plunder::Session::GetPlunder() const
{
	return plunder;
}



/**
 * Get a specific item that can be plundered from the victim ship.
 *
 * @param index The index of the item to get.
 *
 * @return A Plunder object representing the item that can be plundered.
 */
const Plunder &Plunder::Session::GetPlunder(int index) const
{
  return plunder[index];
}



/**
 * Take as much valuable plunder as possible from the victim ship.
 * This function performs the entire plundering process in one go, such
 * as when one AI-controlled ship plunders another.
 *
 * Iterates through the list of plunder from most to least valuable per
 * mass, and takes as much as possible from each item. Stops when the
 * attacker's cargo hold is full or there is no more plunder to take.
 */
void Plunder::Session::Raid()
{
	for(size_t i = 0; i < plunder.size(); ++i)
	{
		Take(i);

		if(attacker->Cargo().Free() < 1)
			break;
	}

	for(size_t i = 0; i < plunder.size(); ++i)
	{
		if(plunder[i].Count() < 1)
			plunder.erase(plunder.begin() + i--);
	}
}



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
	Plunder &item = plunder[index];
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
		int fromCargo = victim->Cargo().Remove(outfit, takenCount);
		// There isn't a RemoveOutfit function, so we call AddOutfit with a negative.
		victim->AddOutfit(outfit, fromCargo - takenCount);

		// Update the total number of outfits that have been taken so far.
		totalOutfitsTaken += takenCount;
	}
	else // This is a commodity.
	{
		takenCount = victim->Cargo().Transfer(item.Name(), available, attacker->Cargo());

		// Update the total mass of commodities that have been taken so far.
		totalCommodityMassTaken += takenCount * item.UnitMass();
	}

	// If pruneList is true and all of the plunder of this type was taken,
	// remove it from the list. Otherwise, just update the count in the list item.
	if(pruneList && takenCount == available)
	{
		plunder.erase(plunder.begin() + index);
		index = min<int>(index, plunder.size());
	}
	else
		item.Remove(takenCount);

	// Update the total mass of the plunder that has been taken so far.
	totalMassTaken += takenCount * item.UnitMass();

	// Update the total value of the plunder that has been taken so far.
	totalValueTaken += takenCount * item.UnitValue();

	// Add the taken items to the list of plunder that has been taken so far.
	taken.emplace_back(item.Name(), takenCount, item.UnitValue());

	return takenCount;
}



/**
 * Get a message describing the result of the plunder session.
 * This message is intended to be displayed to the player after one of
 * their escorts has successfully plundered a disabled ship.
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

	message += " from \"" + victim->Name()
		+ "\" for a total value of " + Format::Credits(totalValueTaken)
		+ " credits, and has " + Format::MassString(attacker->Cargo().Free()) + " of remaining cargo space.";

	if(attacker->Cargo().Free() < 1)
		message += "\"" + attacker->Name() + "\" has no more cargo space remaining.";
	else
		message += attacker->Name() + " has " + Format::MassString(attacker->Cargo().Free()) + " of cargo space remaining.";


	if(plunder.empty())
		message += "\n\"" + victim->Name() + "\" has nothing left that can be plundered.";

	return message;
}



/**
 * Get a list of all of the plunder that was taken.
 *
 * @return A vector of Plunder objects representing the items that were taken.
 *
 */
const std::vector<Plunder> &Plunder::Session::GetTaken() const
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
