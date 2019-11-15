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
	isEscortOnly = false;
	isFlagshipOnly = false;
	isPaidWhileParked = false;
	dailySalary = 100;
	minimumPerShip = 0;
	populationPerOccurrence = 0;
	
	for(const DataNode &child : node)
	{
		if(child.Size() >= 2)
		{
			switch(child.Token(0))
			{
				case "isEscortOnly" :
					isEscortOnly = child.Value(1);
					break;
				case "isFlagshipOnly" :
					isFlagshipOnly = child.Value(1);
					break;
				case "isPaidWhileParked" :
					isPaidWhileParked = child.Value(1);
					break;
				case "dailySalary" :
					dailySalary = child.Value(1);
					break;
				case "minimumPerShip" :
					minimumPerShip = child.Value(1);
					break;
				case "populationPerOccurrence" :
					populationPerOccurrence = child.Value(1);
					break;
				case "name" :
					name = child.Value(1);
					break;
				default :
					child.PrintTrace("Skipping unrecognized attribute:");
			}
		}
		else
			child.PrintTrace("Skipping incomplete attribute:");
	}
}

int64_t Crew::CalculateSalaries(const Ship *flagship, const vector<shared_ptr<Ship>> ships)
{
	const crewDefinitions = GameData::Crew()
	int64_t totalSalaries = 0;

	for(const shared_ptr<Ship> &ship : ships)
		if(!ship->IsDestroyed()) {
			for(const shared_ptr<Crew> &crew : crewDefinitions)
			{
				int64_t count = ship->RequiredCrew()
			}
			// Every ship needs a pilot.
			pilots += 1;
			// We need juniorOfficers to manage our regular crew.
			// If we ever support hiring more crew for escorts, we should use ship->Crew() for these.
			juniorOfficers += ship->RequiredCrew() / GameData::Crew().Get("Crew Per Junior Officer").Value();
			// This is easier than omitting seniorOfficers and juniorOfficers as we go.
			totalCrew += ship->RequiredCrew();
		}

	// Add any extra crew from the flagship.
	if(flagship)
		totalCrew += flagship->Crew() - flagship->RequiredCrew();

	// We don't need a commander for the flagship. We command it directly.
	totalSalaries += (seniorOfficers - 1) * CREDITS_PER_COMMANDER;

	totalSalaries += juniorOfficers * CREDITS_PER_OFFICER;

	// seniorOfficers and juniorOfficers are not regular crew members.
	totalSalaries += (totalCrew - seniorOfficers - juniorOfficers) * CREDITS_PER_REGULAR;

	return totalSalaries;
}



const bool &isEscortOnly() const
{
	return isEscortOnly;
}



const bool &isFlagshipOnly() const
{
	return isFlagshipOnly;
}



const int64_t &DailySalary() const
{
	return DailySalary;
}



const int64_t &MinimumPerShip() const
{
	return MinimumPerShip;
}



const int64_t &PopulationPerOccurrence() const
{
	return PopulationPerOccurrence;
}



const std::string &Name() const
{
	return Name;
}
