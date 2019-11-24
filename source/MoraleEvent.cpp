/* MoraleEvent.cpp
Copyright (c) 2019 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include "MoraleEvent.h"
#include "DataNode.h"
#include "Files.h"
#include "GameData.h"
#include "PlayerInfo.h"

using namespace std;

void MoraleEvent::Load(const DataNode &node)
{
	if(node.Size() >= 2)
		id = node.Token(1);
	
	for(const DataNode &child : node)
	{
		if(child.Size() >= 2)
		{
			if(child.Token(0) == "base chance")
				baseChance = child.Value(1);
			else if(child.Token(0) == "change")
				moraleChange = child.Value(1);
			else if(child.Token(0) == "threshold")
				threshold = child.Value(1);
			else if(child.Token(0) == "chance per morale")
				chancePerMorale = child.Value(1);
			else if(child.Token(0) == "message")
				message = child.Token(1);
			else
				child.PrintTrace("Skipping unrecognized attribute:");
		}
		else
			child.PrintTrace("Skipping incomplete attribute:");
	}
}

double MoraleEvent::ProfitShared(const PlayerInfo &player, const shared_ptr<Ship> &ship, const int64_t sharedProfit)
{
	const string moraleEventId = ship->IsParked()
		? "profit shared on shore leave"
		: "profit shared";
	
	const MoraleEvent * moraleEvent = GameData::MoraleEvents().Get(moraleEventId);
	if(!moraleEvent)
	{
		Files::LogError("\nMissing \"morale event\" definition: \"" + moraleEventId + "\"");
		return 0;
	}
	
	Files::LogError("Ship: " + ship->Name() + ", Crew(): " + to_string(ship->Crew()) + ", IsParked(): " + to_string(ship->IsParked()));
	
	double profitPerCrewMember = sharedProfit / (double)ship->Crew();
	
	Files::LogError("profitPerCrewMember: " + to_string(profitPerCrewMember));
	double moraleChange = moraleEvent->MoraleChange() * profitPerCrewMember;
	Files::LogError("moraleChange: " + to_string(moraleChange) + ", MoraleChange(): " + to_string(moraleEvent->MoraleChange()));
	
	return player.ChangeShipMorale(ship.get(), moraleChange);
}

double MoraleEvent::BaseChance() const
{
	return chancePerMorale;
}



double MoraleEvent::ChancePerMorale() const
{
	return chancePerMorale;
}



double MoraleEvent::MoraleChange() const
{
	return moraleChange;
}



double MoraleEvent::Threshold() const
{
	return threshold;
}



const string &MoraleEvent::Id() const
{
	return id;
}



const string &MoraleEvent::Message() const
{
	return message;
}
