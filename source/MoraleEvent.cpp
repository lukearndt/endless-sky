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
			else if(child.Token(0) == "effect")
				effect = child.Value(1);
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

double MoraleEvent::BaseChance() const
{
	return chancePerMorale;
}



double MoraleEvent::ChancePerMorale() const
{
	return chancePerMorale;
}



double MoraleEvent::Effect() const
{
	return effect;
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
