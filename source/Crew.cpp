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
#include "Files.h" 
#include "Format.h" 
#include "GameData.h"
#include "MoraleEvent.h"

using namespace std;

void Crew::Load(const DataNode &node)
{
	if(node.Size() >= 2)
		id = node.Token(1);
	
	for(const DataNode &child : node)
	{
		if(child.Size() >= 2)
		{
			if(child.Token(0) == "name")
				name = child.Token(1);
			else if(child.Token(0) == "minimum per ship")
				minimumPerShip = max((int)child.Value(1), 0);
			else if(child.Token(0) == "parked salary")
				parkedSalary = max((int)child.Value(1), 0);
			else if(child.Token(0) == "parked shares")
				parkedShares = max((int)child.Value(1), 0);
			else if(child.Token(0) == "population per member")
				populationPerMember = max((int)child.Value(1), 0);
			else if(child.Token(0) == "salary")
				salary = max((int)child.Value(1), 0);
			else if(child.Token(0) == "shares")
				shares = max((int)child.Value(1), 0);
			else
				child.PrintTrace("Skipping unrecognized attribute:");
		}
		else if(child.Token(0) == "avoids escorts")
			avoidsEscorts = true;
		else if(child.Token(0) == "avoids flagship")
			avoidsFlagship = true;
		else
			child.PrintTrace("Skipping incomplete attribute:");
	}
}



int64_t Crew::CalculateSalaries(const vector<shared_ptr<Ship>> &ships, const Ship * flagship, const bool includeExtras)
{
	bool checkIfFlagship = true;
	bool isFlagship = false;
	int64_t totalSalaries = 0;
	
	for(const shared_ptr<Ship> &ship : ships)
	{
		if(checkIfFlagship)
			isFlagship = ship.get() == flagship;
		
		totalSalaries += Crew::SalariesForShip(
			ship,
			isFlagship,
			includeExtras
		);
		
		if(isFlagship)
			isFlagship = checkIfFlagship = false;
	}
	
	return totalSalaries;
}



int64_t Crew::CostOfExtraCrew(const vector<shared_ptr<Ship>> &ships, const Ship * flagship)
{
	// Calculate with and without extras and return the difference.
	return Crew::CalculateSalaries(ships, flagship, true)
		- Crew::CalculateSalaries(ships, flagship, false);
}



int64_t Crew::NumberOnShip(const Crew &crew, const shared_ptr<Ship> &ship, const bool isFlagship, const bool includeExtras)
{
	int64_t count = 0;
	
	// If this is the flagship, check if this crew avoids the flagship.
	if(isFlagship && crew.AvoidsFlagship())
		return count;
	// If this is an escort, check if this crew avoids escorts.
	if(!isFlagship && crew.AvoidsEscorts())
		return count;
	
	const int64_t countableCrewMembers = includeExtras
		? ship->Crew()
		: ship->RequiredCrew();
	
	// Apply the per-ship minimum.
	 count = min(crew.MinimumPerShip(), countableCrewMembers);
	
	// Prevent division by zero so that the universe doesn't implode.
	if(crew.PopulationPerMember())
	{
		// Figure out how many of this kind of crew we have, by population.
		count = max(
			count,
			countableCrewMembers / crew.PopulationPerMember()
		);
	}
	
	return count;
}


double Crew::SharesForShip(
	const shared_ptr<Ship> &ship,
	const bool isFlagship,
	const bool includeExtras
)
{
	int64_t totalShares = 0;
	
	// Add up the salaries for all of the special crew members
	for(const pair<const string, Crew>& crewPair : GameData::Crews())
	{
		// Skip the default crew members.
		if(crewPair.first == "default")
			continue;
		
		const Crew crew = crewPair.second;
		// Figure out how many of this type of crew are on this ship
		int numberOnShip = Crew::NumberOnShip(
			crew,
			ship,
			includeExtras
		);
		
		// Add this type of crew member's shares to the result
		totalShares += numberOnShip * (ship->IsParked()
			? crew.ParkedShares()
			: crew.Shares());
	}
	
	return totalShares;
}



int64_t Crew::SalariesForShip(const shared_ptr<Ship> &ship, const bool isFlagship, const bool includeExtras)
{
	// We don't need to pay dead people.
	if(ship->IsDestroyed())
		return 0;
	
	int64_t salariesForShip = 0;
	int64_t specialCrewMembers = 0;
	
	// Add up the salaries for all of the special crew members
	for(const pair<const string, Crew> &crewPair : GameData::Crews())
	{
		// Skip the default crew members.
		if(crewPair.first == "default")
			continue;
		
		const Crew crew = crewPair.second;
		// Figure out how many of this type of crew are on this ship
		int numberOnShip = Crew::NumberOnShip(
			crew,
			ship,
			isFlagship,
			includeExtras
		);
		
		specialCrewMembers += numberOnShip;
		
		// Add this type of crew member's salaries to the result
		salariesForShip += numberOnShip * (ship->IsParked()
			? crew.ParkedSalary()
			: crew.Salary());
	}
	
	// Figure out how many regular crew members are left over
	int64_t defaultCrewMembers = (
		includeExtras 
			? ship->Crew()
			: ship->RequiredCrew()
		) - specialCrewMembers
		// If this is the flagship, one of the crew members is the Captain
		- isFlagship;

	const Crew *defaultCrew = GameData::Crews().Find("default");
	
	if(!defaultCrew)
	{
		defaultCrew = new Crew();
		Files::LogError("\nWarning: No default crew member defined in data files");
	}
	
	// Add default crew members' salaries to the result
	salariesForShip += defaultCrewMembers * (ship->IsParked()
		? defaultCrew->ParkedSalary()
		: defaultCrew->Salary());
	
	return salariesForShip;
}



int64_t Crew::ShareProfit(
	const PlayerInfo &player,
	const int64_t grossProfit
)
{
	if(grossProfit <= 0) return 0;
	
	// We don't want to keep checking for the flagship once we find it.
	bool checkIfFlagship = true;
	bool isFlagship = false;
	
	// We don't want to calculate the ships' crew shares more than once,
	// so let's cache them in an array for the second step of the process.
	Files::LogError("\nabout to crewSharesCache");
	int playerShipCount = player.Ships().size();
	Files::LogError("playerShipCount: " + to_string(playerShipCount));
	
	int64_t crewSharesCache [playerShipCount];
	int64_t totalCrewShares = 0;
	
	for(size_t index = 0; index != player.Ships().size(); ++index)
	{
		// Calculate how many shares this ship has in total.
		const shared_ptr<Ship> &ship = player.Ships()[index];
		if(checkIfFlagship)
			isFlagship = ship.get() == player.Flagship();
		
		int64_t crewShares = Crew::SharesForShip(
			ship,
			isFlagship
		);

		if(isFlagship)
			isFlagship = checkIfFlagship = false;
		
		crewSharesCache[index] = crewShares;
		totalCrewShares += crewShares;
		Files::LogError("crewShares: " + to_string(crewShares) + ", totalCrewShares: " + to_string(totalCrewShares));
	}
	Files::LogError("calculated crew shares for all ships");
	for(const int64_t shares : crewSharesCache)
	{
		Files::LogError("crewSharesCache shares in cache: " + to_string(shares));
	}
	Files::LogError("end of crewSharesCache");
	
	// Calculate how many shares are in the entire fleet.
	double totalFleetShares = Crew::CAPTAIN_SHARES + totalCrewShares;
	
	for(size_t index = 0; index != player.Ships().size(); ++index)
	{
		Files::LogError("\n");
		const shared_ptr<Ship> &ship = player.Ships()[index];
		// Calculate how much of the profit we're giving to this ship's crew
		int64_t crewShares = crewSharesCache[index];
		Files::LogError("crewShares: " + to_string(crewShares));
		
		int64_t sharedProfit = grossProfit * crewShares / totalFleetShares;
		Files::LogError("sharedProfit: " + Format::Credits(sharedProfit) + " of grossProfit: " + Format::Credits(grossProfit));
		
		// Trigger a morale event for the shared profit
		Files::LogError("ship morale before: " + to_string(ship->Morale()));
		double returnedMorale = MoraleEvent::ProfitShared(player, ship, sharedProfit);
		Files::LogError("returnedMorale: " + to_string(returnedMorale));
		Files::LogError("ship morale after: " + to_string(ship->Morale()));
		
	}
	
	return grossProfit * totalCrewShares / totalFleetShares;
}



bool Crew::AvoidsEscorts() const
{
	return avoidsEscorts;
}



bool Crew::AvoidsFlagship() const
{
	return avoidsFlagship;
}



double Crew::Shares() const
{
	return shares;
}



int64_t Crew::MinimumPerShip() const
{
	return minimumPerShip;
}



int64_t Crew::ParkedSalary() const
{
	return parkedSalary;
}



int64_t Crew::ParkedShares() const
{
	return parkedShares;
}



int64_t Crew::PopulationPerMember() const
{
	return populationPerMember;
}



int64_t Crew::Salary() const
{
	return salary;
}



const string &Crew::Id() const
{
	return id;
}



const string &Crew::Name() const
{
	return name;
}
