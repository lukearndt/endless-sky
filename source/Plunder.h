/* Plunder.h
Extracted from BoardingPanel.h, copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "Outfit.h"
#include "Ship.h"

#include <memory>
#include <string>
#include <vector>

class Outfit;
class Ship;


// This class represents an item that can be plundered from a ship.
class Plunder {
public:
	// Plunder can be either outfits or commodities.
	Plunder(const std::string &commodity, int count, int unitValue);
	Plunder(const Outfit *outfit, int count);

	class Session {
	public:
		Session(std::shared_ptr<Ship> &target, std::shared_ptr<Ship> &attacker, const std::vector<std::shared_ptr<Ship>> &attackerFleet);

		static std::vector<std::shared_ptr<Plunder>> BuildPlunderList(const std::shared_ptr<Ship> &ship);

		// Get the list of items that can be plundered from the target ship.
		const std::vector<std::shared_ptr<Plunder>> &RemainingPlunder() const;

		// Take as much valuable plunder as possible from the target ship.
		// TODO: Move to BoardingCombat.
		// void Raid();

		// Take an item from the target and give it on the attacker.
		// If no quantity is specified, take as many as possible.
		// Returns how many were successfully taken.
		int Take(int index, bool pruneList = false, int quantity = -1);

		// Get a message describing the result of the plunder session.
		const std::string GetSummary() const;

		// Get a list of all of the plunder that has been taken.
		const std::vector<std::shared_ptr<Plunder>> &TakenPlunder() const;

		// Refresh the list of plunder that can be taken. Call this if the target
		// has been successfully boarded and all its crew members killed.

		// Get the total mass of the plunder that was taken.
		int64_t TotalCommodityMassTaken() const;

		// Get the total mass of the plunder that was taken.
		int64_t TotalMassTaken() const;

		// Get the total mass of the plunder that was taken.
		int64_t TotalOutfitsTaken() const;

		// Get the total value of all the plunder that was taken.
		int64_t TotalValueTaken() const;

	private:
		std::shared_ptr<Ship> attacker;
		std::vector<std::shared_ptr<Ship>> attackerFleet;
		std::shared_ptr<Ship> target;
		std::vector<std::shared_ptr<Plunder>> remaining;
		std::vector<std::shared_ptr<Plunder>> taken;
		int64_t totalCommodityMassTaken;
		int64_t totalMassTaken;
		int64_t totalOutfitsTaken;
		int64_t totalValueTaken;
	};

	// Sort by value per ton of mass.
	bool operator<(const Plunder &other) const;

	// Check how many of this item are left un-plundered. Once this is zero,
	// the item can be removed from the list.
	int Count() const;
	// Get the value of each unit of this plunder item.
	int64_t UnitValue() const;

	// Get the name of this item. If it is a commodity, this is its name.
	const std::string &Name() const;
	// Get the mass, in the format "<count> x <unit mass>". If this is a
	// commodity, no unit mass is given (because it is 1). If the count is
	// 1, only the unit mass is reported.
	const std::string &Size() const;
	// Get the total value (unit value times count) as a string.
	const std::string &Value() const;

	// If this is an outfit, get the outfit. Otherwise, this returns null.
	const Outfit *GetOutfit() const;

	bool HasEnoughSpace(const std::shared_ptr<Ship> &ship) const;

	bool RequiresConquest() const;

protected:
	int UpdateCount(int amount);

private:
	void UpdateStrings();
	double UnitMass() const;

private:
	std::string name;
	const Outfit *outfit;
	int count;
	int64_t unitValue;
	std::string size;
	std::string value;
};

