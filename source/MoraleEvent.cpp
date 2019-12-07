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
#include "Crew.h"
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



void MoraleEvent::CrewMemberDeath(const PlayerInfo &player, const shared_ptr<Ship> &ship, const int64_t deathCount)
{
	if(!ship->IsDestroyed())
		MoraleEvent::DeathOnShip(player, ship, deathCount);
	
	MoraleEvent::DeathInFleet(player, deathCount);	
}



void MoraleEvent::DeathInFleet(const PlayerInfo &player, const int64_t deathCount)
{
	const MoraleEvent * moraleEvent = GetMoraleEvent("death in fleet");
	if(!moraleEvent || moraleEvent->MoraleChange() == 0)
		return;
	
	return player.ChangeFleetMorale(moraleEvent->MoraleChange() * deathCount);
}



double MoraleEvent::DeathOnShip(const PlayerInfo &player, const shared_ptr<Ship> &ship, const int64_t deathCount)
{
	const MoraleEvent * moraleEvent = GetMoraleEvent("death on ship");
	if(!moraleEvent || moraleEvent->MoraleChange() == 0)
		return ship->Morale();
	
	return player.ChangeShipMorale(ship.get(), moraleEvent->MoraleChange() * deathCount);
}



double MoraleEvent::ProfitShared(const PlayerInfo &player, const shared_ptr<Ship> &ship, const int64_t sharedProfit)
{
	const MoraleEvent * moraleEvent = GetMoraleEvent(ship->IsParked()
		? "profit shared on shore leave"
		: "profit shared"
	);
	if(!moraleEvent || moraleEvent->MoraleChange() == 0)
		return ship->Morale();

	return player.ChangeShipMorale(
		ship.get(),
		moraleEvent->MoraleChange() * sharedProfit / (double)ship->Crew()
	);
}



void MoraleEvent::SalaryFailure(const PlayerInfo &player)
{
	const MoraleEvent * moraleEvent = GetMoraleEvent("salary failure");
	if(!moraleEvent || moraleEvent->MoraleChange() == 0)
	  return;
	
	for(const shared_ptr<Ship> &ship : player.Ships())
	{
		int64_t shipSalary = Crew::SalariesForShip(
			ship,
			ship.get() == player.Flagship()
		);
		
		if(shipSalary > 0)
			player.ChangeShipMorale(ship.get(), moraleEvent->MoraleChange());
	}
}



void MoraleEvent::SalaryPayment(const PlayerInfo &player)
{
	for(const shared_ptr<Ship> &ship : player.Ships())
	{
		if(ship->IsParked())
			ShipSalaryPaymentParked(player, ship);
		else
			ShipSalaryPaymentActive(player, ship);
	}
}



double MoraleEvent::ShipSalaryPaymentActive(const PlayerInfo &player, const shared_ptr<Ship> &ship)
{
	const MoraleEvent * moraleEvent = GetMoraleEvent("salary payment");
	if(!moraleEvent || moraleEvent->MoraleChange() == 0)
		return ship->Morale();
	
	return player.ChangeShipMorale(ship.get(), moraleEvent->MoraleChange());
}



double MoraleEvent::ShipSalaryPaymentParked(const PlayerInfo &player, const shared_ptr<Ship> &ship)
{
	const MoraleEvent * moraleEvent = GetMoraleEvent("salary payment on shore leave");
	if(!moraleEvent || moraleEvent->MoraleChange() == 0)
		return ship->Morale();
	
	const int64_t salariesPaid = Crew::SalariesForShip(
		ship,
		ship.get() == player.Flagship()
	);
	
	if(salariesPaid > 0)
		return player.ChangeShipMorale(
			ship.get(),
			moraleEvent->MoraleChange() * salariesPaid / (double)ship->Crew()
		);
	else
		return ship->Morale();
}



const MoraleEvent * MoraleEvent::GetMoraleEvent(const std::string &moraleEventId)
{
	const MoraleEvent * moraleEvent = GameData::MoraleEvents().Get(moraleEventId);
	if(!moraleEvent || moraleEvent->MoraleChange() == 0)
		Files::LogError("\nMissing \"morale event\" definition: \"" + moraleEventId + "\"");
	return moraleEvent;
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
