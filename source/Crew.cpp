/* Crew.cpp
Copyright (c) 2019 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include "Crew.h"
#include "GameData.h"

using namespace std;

// Load definition of a crew member
void Crew::Load(const DataNode &node)
{
	// Set the name of this type of crew member, so we know it has been loaded.
	if(node.Size() >= 2)
		name = node.Token(1);

	// Set default values so that we don't have to specify every node.
	isOnEscorts = true;
	isOnFlagship = true;
	isPaidSalaryWhileParked = false;
	dailySalary = 100;
	minimumPerShip = 0;
	populationPerOccurrence = 0;
	
	for(const DataNode &child : node)
	{
		if(child.Size() >= 2)
		{
			if(child.Token(0) == "on escorts?")
				isOnEscorts = child.Value(1);
			else if(child.Token(0) == "on flagship?")
				isOnFlagship = child.Value(1);
			else if(child.Token(0) == "pay salary while parked?")
				isPaidSalaryWhileParked = child.Value(1);
			else if(child.Token(0) == "daily salary")
				dailySalary = child.Value(1);
			else if(child.Token(0) == "minimum per ship")
				minimumPerShip = child.Value(1);
			else if(child.Token(0) == "population per occurrence")
				populationPerOccurrence = child.Value(1);
			else if(child.Token(0) == "name")
				name = child.Token(1);
			else
				child.PrintTrace("Skipping unrecognized attribute:");
		}
		else
			child.PrintTrace("Skipping incomplete attribute:");
	}
}

int64_t Crew::CalculateSalaries(const Ship *flagship, const vector<shared_ptr<Ship>> ships)
{
	bool payDefaultWhileParked = false;
	const Set<Crew> crews = GameData::Crews();
	const Crew *defaultCrew = crews.Find("default");
	int64_t totalSalaries = 0;

	for(const shared_ptr<Ship> &ship : ships)
		if(!ship->IsDestroyed())
		{
			int specialCrew = 0;
			
			for(const pair<const string, Crew> crewPair : crews)
			{
				const Crew crew = crewPair.second;
				int numberOnShip = Crew::NumberOnShip(
					crew,
					ship,
					ship->Name() == flagship->Name()
				);

				specialCrew += numberOnShip;
				totalSalaries += numberOnShip * crew.DailySalary();
			}

			// Now that we've counted the special crew members, we can pay the rest
			if(payDefaultWhileParked || !ship->IsParked())
				totalSalaries += (ship->Crew() - specialCrew) * defaultCrew->DailySalary();
		}

	// Subtract one default crew member salary because we don't need to pay ourselves
	return totalSalaries - defaultCrew->DailySalary();
}

int64_t Crew::NumberOnShip(const Crew crew, const shared_ptr<Ship> ship, const bool isFlagship)
{
	// Is this the flagship, and does this crew appear on the flagship?
	if(isFlagship && !crew.IsOnFlagship())
		return 0;
	// Is this an escort, and does this crew appear on escorts?
	if(!isFlagship && !crew.IsOnEscorts())
		return 0;
	// Is the ship parked, and do we pay salary to this crew while parked?
	if(ship->IsParked() && !crew.IsPaidSalaryWhileParked())
		return 0;
	
	// Initialise the count with the minimum per ship, or total crew if lower
	int64_t count = min((int)crew.MinimumPerShip(), ship->Crew());

	// Guard against division by zero
	if(crew.PopulationPerOccurrence())
	{
		// Figure out how many of this kind of crew we have, by population
		count = max(count, ship->Crew() / crew.PopulationPerOccurrence());
	}

	return count;
}


const bool &Crew::IsOnEscorts() const
{
	return isOnEscorts;
}



const bool &Crew::IsOnFlagship() const
{
	return isOnFlagship;
}



const bool &Crew::IsPaidSalaryWhileParked() const
{
	return isPaidSalaryWhileParked;
}



const int64_t &Crew::DailySalary() const
{
	return dailySalary;
}



const int64_t &Crew::MinimumPerShip() const
{
	return minimumPerShip;
}



const int64_t &Crew::PopulationPerOccurrence() const
{
	return populationPerOccurrence;
}



const string &Crew::Name() const
{
	return name;
}
