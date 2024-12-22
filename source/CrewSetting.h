/* CrewSetting.h
Copyright (c) 2024 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

#ifndef CREW_SETTING_H_
#define CREW_SETTING_H_

#include "DataNode.h"

#include <map>

class CrewSetting
{
public:
	// Load a definition for a crew setting.
	void Load(const DataNode &node);

	static const double DeathBenefitSalaryMultiplier();
	static const double DeathSharesMultiplier();
	static const int64_t SalaryPerShare();
	static const std::string RankingCrewId(const std::string &category);
	static const std::vector<std::string> OutrankingCrewIds(const std::string &rankingCrewId);
	static const int64_t PlayerSharesBase();
	static const int64_t PlayerSharesMinimum();
	static const int64_t PlayerSharesPerCombatLevel();
	static const double PlayerSharesPerCreditRating();
	static const int64_t PlayerSharesPerLicense();

	const std::string &Id() const;
	const std::string &Name() const;

private:

	// The id that the crew setting is stored against in GameData::CrewSettings()
	std::string id;
	// The display name for this crew setting
	std::string name;
	// This map is generated from the "ranking crew by ship category" crew setting.
	std::map<std::string, std::string> rankingCrewIdByShipCategory;
	// This map is generated from the "command structure" crew setting.
	std::map<std::string, std::vector<std::string>> outrankingByRank;
	// This is multiplied by the salary of a crew member to determine their default death benefit.
	double deathBenefitSalaryMultiplier = 0.0;
	// This is multiplied by the shares of a crew member to determine their default death shares.
	double deathSharesMultiplier = 0.0;
	// The base number of shares that the player has before any other factors.
	int64_t playerSharesBase = 0;
	// The minimum number of shares that they player can hold.
	int64_t playerSharesMinimum = 0;
	// The number of shares that the player gains for each level of combat rating.
	int64_t playerSharesPerCombatLevel = 0;
	// The number of shares that the player gains for each point of credit score.
	double playerSharesPerCreditRating = 0.0;
	// The number of shares that the player gains for each license that they have earned.
	int64_t playerSharesPerLicense = 0;
	// This is used to convert shares to salary when the "Profit sharing"
	// difficulty setting is set to "converted".
	int64_t salaryPerShare = 0;
};

#endif
