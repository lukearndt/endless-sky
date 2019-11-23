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

class MoraleEvent
{
public:
	// Load a definition for a morale event
	void Load(const DataNode &node);
	
	// Profit has been shared with the crew on the ship
	// Uses "profit shared on shore leave" or "profit shared" events
	// MoraleChange is multiplied by sharedProfit and divided by crew count
	static double ProfitShared(std::shared_ptr<Ship> &ship, const int64_t sharedProfit);
	
	double BaseChance() const;
	double ChancePerMorale() const;
	double MoraleChange() const;
	double Threshold() const;
	const std::string &Id() const;
	const std::string &Message() const;

private:
	// The base chance of an active event happening
	double baseChance = 0;
	// The chance of an active event increases per point of morale past the threshold
	double chancePerMorale = 0;
	// The event changes the ship's morale by this much when it occurs
	double moraleChange = 0;
	// The morale at which an active event becomes possible
	double threshold = 0;
	// The id that the morale event is stored against in GameData::Crews()
	std::string id;
	// A message to display when the morale event occurs
	std::string message;
};

#endif
