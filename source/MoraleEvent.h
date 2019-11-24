/* MoraleEvent.h
Copyright (c) 2019 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#ifndef MORALE_EVENT_H_
#define MORALE_EVENT_H_

#include "DataNode.h"
#include "Ship.h"
#include "PlayerInfo.h"

class MoraleEvent
{
public:
	// Load a definition for a morale event
	void Load(const DataNode &node);
	
	// One or more crew members have died in the fleet
	// Uses "death in fleet" and "death on ship" events
	// "death on fleet" MoraleChange is applied to every ship in the fleet
	// "death on ship" MoraleChange is applied to the ship where the crew member died
	// In both cases, MoraleChange is multiplied by how many crew members died
	static void CrewMemberDeath(const PlayerInfo &player, const std::shared_ptr<Ship> &ship, const int64_t deathCount);
	
	// Profit has been shared with the crew on the ship
	// Uses "profit shared on shore leave" or "profit shared" events
	// MoraleChange is multiplied by sharedProfit and divided by crew count
	static double ProfitShared(const PlayerInfo &player, const std::shared_ptr<Ship> &ship, const int64_t sharedProfit);
	
	// The captain has failed to pay crew salaries
	// Uses "salary failure" event
	// MoraleChange is applied to every ship that has crew members that were
	// supposed to be paid today
	static void SalaryPaid(const PlayerInfo &player);
	
	// The captain has paid crew salaries
	// Uses "salary failure" event
	// MoraleChange is applied to every ship that has crew members that were
	// supposed to be paid today
	static void SalaryFailure(const PlayerInfo &player);
	
	double BaseChance() const;
	double ChancePerMorale() const;
	double IsUndefined() const;
	double MoraleChange() const;
	double Threshold() const;
	const std::string &Id() const;
	const std::string &Message() const;

private:
	static const MoraleEvent * GetMoraleEvent(std::string moraleEventId);
	static void DeathInFleet(const PlayerInfo &player, const int64_t deathCount);
	static double DeathOnShip(const PlayerInfo &player, const std::shared_ptr<Ship> &ship, const int64_t deathCount);
	
	// For events that change a ship's morale (eg shared profit):
	
	// The event changes the ship's morale by this much when it occurs
	double moraleChange = 0;



	// For events that occur as a result of morale (eg mutiny):
	
	// The base chance of the event occurring
	double baseChance = 0;
	// The chance increases per point of morale past the threshold
	double chancePerMorale = 0;
	// The morale at which the reactionary event becomes possible
	double threshold = 0;


	
	// For all morale events:
	
	// The id that the morale event is stored against in GameData::Crews()
	std::string id;
	// A message to display when the morale event occurs
	std::string message;
};

#endif
