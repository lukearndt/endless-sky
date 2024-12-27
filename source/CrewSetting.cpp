/* Crew.cpp
Copyright (c) 2024 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

#include "CrewSetting.h"

#include "CategoryList.h"
#include "GameData.h"
#include "Logger.h"
#include "PlayerInfo.h"

using namespace std;

void CrewSetting::Load(const DataNode &node)
{
	if(node.Size() >= 2)
	{
		id = node.Token(1);
		name = id;
	}

	if(id == "ranking crew by ship category")
	{
		for (const DataNode &child : node)
		{
			if(child.Size() != 2) {
				child.PrintTrace("Skipping malformed attribute:");
				continue;
			}

			rankingCrewIdByShipCategory[child.Token(0)] = child.Token(1);
		}
	}

	if(id == "command structure")
	{
		vector<string> outrankingCrewIds = {};
		outrankingByRank.clear();

		for(const DataNode &child : node)
		{
			if(child.Size() != 1) {
				child.PrintTrace("Skipping malformed attribute:");
				continue;
			}

			for (const string &outrankingCrewId : outrankingCrewIds)
			{
				outrankingByRank[child.Token(0)].push_back(outrankingCrewId);
			}

			outrankingCrewIds.push_back(child.Token(0));
		}
	}

	if(id == "death benefit salary multiplier")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		deathBenefitSalaryMultiplier = node.Value(2);
	}

	if(id == "death shares multiplier")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		deathSharesMultiplier = node.Value(2);
	}

	if(id == "conversion ratio: salary per share")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		salaryPerShare = node.Value(2);
	}

	if(id == "player shares base")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		playerSharesBase = node.Value(2);
	}

	if(id == "player shares minimum")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		playerSharesMinimum = node.Value(2);
	}

	if(id == "player shares per combat level")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		playerSharesPerCombatLevel = node.Value(2);
	}

	if(id == "player shares per credit score")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		playerSharesPerCreditRating = node.Value(2);
	}

	if(id == "player shares per license")
	{
		if(node.Size() != 3) {
			node.PrintTrace("Skipping malformed node:");
			return;
		}

		playerSharesPerLicense = node.Value(2);
	}
}


const std::string CrewSetting::RankingCrewId(const std::string &shipCategory)
{
	return GameData::CrewSettings().Get("ranking crew by ship category")->rankingCrewIdByShipCategory.at(shipCategory);
}



const std::vector<std::string> CrewSetting::OutrankingCrewIds(const std::string &rankingCrewId)
{
	auto outrankingCrewIds = GameData::CrewSettings().Get("command structure")->outrankingByRank;

	auto it = outrankingCrewIds.find(rankingCrewId);

	if(it != outrankingCrewIds.end())
		return it->second;
	else
		return {};
}



const int64_t CrewSetting::PlayerSharesBase()
{
  return GameData::CrewSettings().Get("player shares base")->playerSharesBase;
}



const int64_t CrewSetting::PlayerSharesMinimum()
{
  return GameData::CrewSettings().Get("player shares minimum")->playerSharesMinimum;
}



const int64_t CrewSetting::PlayerSharesPerCombatLevel()
{
  return GameData::CrewSettings().Get("player shares per combat level")->playerSharesPerCombatLevel;
}



const double CrewSetting::PlayerSharesPerCreditRating()
{
  return GameData::CrewSettings().Get("player shares per credit score")->playerSharesPerCreditRating;
}



const int64_t CrewSetting::PlayerSharesPerLicense()
{
  return GameData::CrewSettings().Get("player shares per license")->playerSharesPerLicense;
}



const double CrewSetting::DeathBenefitSalaryMultiplier()
{
  return GameData::CrewSettings().Get("death benefit salary multiplier")->deathBenefitSalaryMultiplier;
}



const double CrewSetting::DeathSharesMultiplier()
{
  return GameData::CrewSettings().Get("death shares multiplier")->deathSharesMultiplier;
}



const int64_t CrewSetting::SalaryPerShare()
{
	return GameData::CrewSettings().Get("conversion ratio: salary per share")->salaryPerShare;
}



const std::string &CrewSetting::Id() const
{
	return id;
}



const std::string &CrewSetting::Name() const
{
	return name;
}
