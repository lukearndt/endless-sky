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
	isOnEscorts = false;
	isOnFlagship = false;
	isPaidWhileParked = false;
	dailySalary = 100;
	minimumPerShip = 0;
	populationPerOccurrence = 0;
	
	for(const DataNode &child : node)
	{
		if(child.Size() >= 2)
		{
			if(child.Token(0) == "On Escorts")
				isOnEscorts = child.Value(1);
			else if(child.Token(0) == "On Flagship")
				isOnFlagship = child.Value(1);
			else if(child.Token(0) == "Paid While Parked")
				isPaidWhileParked = child.Value(1);
			else if(child.Token(0) == "Daily Salary")
				dailySalary = child.Value(1);
			else if(child.Token(0) == "Minimum Per Ship")
				minimumPerShip = child.Value(1);
			else if(child.Token(0) == "Population Per Occurrence")
				populationPerOccurrence = child.Value(1);
			else if(child.Token(0) == "Name")
				name = child.Value(1);
			else
				child.PrintTrace("Skipping unrecognized attribute:");
		}
		else
			child.PrintTrace("Skipping incomplete attribute:");
	}
}

int64_t Crew::CalculateSalaries(const Ship *flagship, const vector<shared_ptr<Ship>> ships)
{
	bool payRegularsWhileParked = false;
	const vector<shared_ptr<Crew>> crews = GameData::Crews();
	int64_t dailySalaryForRegulars = 100;
	int64_t totalSalaries = 0;

	for(const shared_ptr<Ship> &ship : ships)
		if(!ship->IsDestroyed())
		{
			int specialCrew = 0;
			
			for(const shared_ptr<Crew> &crew : crews)
			{
				if(crew->Name() == "Regulars")
				{
					dailySalaryForRegulars = crew->DailySalary();
					payRegularsWhileParked = crew->IsPaidWhileParked();
				}
				else if (
					(
						// Is this the flagship, and does this crew appear on the flagship?
						(ship->Name() == flagship->Name() && crew->IsOnFlagship())
						// Is this an escort, and does this crew appear on escorts?
						|| (ship->Name() != flagship->Name() && crew->IsOnEscorts())
					)
					// Do we pay this crew while parked? If not, is the ship active?
					&& crew->IsPaidWhileParked() || !ship->IsParked()
				)
				{
					int64_t count = 0;
					// Guard against division by zero
					if(crew->PopulationPerOccurrence())
					// Figure out how many of this kind of crew we have, by population
						ship->Crew() / crew->PopulationPerOccurrence();
					
					// Enforce the minimum per ship rule
					if(count < crew->MinimumPerShip())
					{
						// But don't exceed the total number of crew on the ship
						if(crew->MinimumPerShip() <= ship->Crew())
							count = crew->MinimumPerShip();
						else
							count = ship->Crew();
					}

					specialCrew += count;
					totalSalaries += count * crew->DailySalary();
				}
			}
			// Now that we've counted the special crew members, we can pay the regulars
			if(payRegularsWhileParked || !ship->IsParked())
				totalSalaries += (ship->Crew() - specialCrew) * dailySalaryForRegulars;
		}

	return totalSalaries;
}



const bool &Crew::IsOnEscorts() const
{
	return isOnEscorts;
}



const bool &Crew::IsOnFlagship() const
{
	return isOnFlagship;
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



const std::string &Crew::Name() const
{
	return name;
}
