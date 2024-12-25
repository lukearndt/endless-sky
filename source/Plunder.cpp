/* Plunder.cpp
Extracted from BoardingPanel.cpp, copyright (c) 2014 by Michael Zahniser

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
#include "Format.h"
#include "Outfit.h"
#include "Ship.h"

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



// Take some or all of this plunder item.
void Plunder::Take(int count)
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
