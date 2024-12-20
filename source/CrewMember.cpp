/* CrewMember.cpp
Copyright (c) 2024 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/


#include "CrewMember.h"

#include "CategoryList.h"
#include "CrewSetting.h"
#include "DataNode.h"
#include "GameData.h"
#include "Logger.h"
#include "Preferences.h"
#include "Ship.h"

using namespace std;

void CrewMember::Load(const DataNode &node)
{
	if(node.Size() >= 2)
	{
		id = node.Token(1);
		name = id;
	}

	bool defaultCategoryInclusion = true;

	for(const DataNode &child : node)
	{
		if(child.Size() >= 2)
		{
			if(child.Token(0) == "name")
				name = child.Token(1);
			else if(child.Token(0) == "occurs at")
				for(int index = 1; index < child.Size(); ++index)
					occursAt.push_back(max((int)child.Value(index), 0));
			else if(child.Token(0) == "death benefit")
				deathBenefit = max((int)child.Value(1), 0);
			else if(child.Token(0) == "death shares")
				deathShares = max((int)child.Value(1), 0);
			else if(child.Token(0) == "parked salary")
				parkedSalary = max((int)child.Value(1), 0);
			else if(child.Token(0) == "parked shares")
				parkedShares = max((int)child.Value(1), 0);
			else if(child.Token(0) == "ship population per member")
				shipPopulationPerMember = max((int)child.Value(1), 0);
			else if(child.Token(0) == "salary")
				salary = max((int)child.Value(1), 0);
			else if(child.Token(0) == "shares")
				shares = max((int)child.Value(1), 0);
			else if(child.Token(0) == "shares per combat level")
				sharesPerCombatLevel = max((int)child.Value(1), 0);
			else if(child.Token(0) == "shares per license")
				sharesPerLicense = max((int)child.Value(1), 0);
			else if(child.Token(0) == "avoids ship categories")
				for(int index = 1; index < child.Size(); ++index)
					avoidsShipCategories.push_back(child.Token(index));
			else if(child.Token(0) == "only ship categories") {
				defaultCategoryInclusion = false;
				for(int index = 1; index < child.Size(); ++index)
					onlyShipCategories.push_back(child.Token(index));
			}
			else
				child.PrintTrace("Skipping unrecognized attribute:");
		}
		else if(child.Token(0) == "avoids escorts")
			avoidsEscorts = true;
		else if(child.Token(0) == "avoids flagship")
			avoidsFlagship = true;
		else if(child.Token(0) == "avoids parked")
			avoidsParked = true;
		else if(child.Token(0) == "only parked")
			onlyParked = true;
		else
			child.PrintTrace("Skipping incomplete attribute:");
	}

	const CategoryList &shipCategoryList = GameData::GetCategory(CategoryType::SHIP);

	for(const CategoryList::Category &category : shipCategoryList) {
		shipCategories[category.Name()] = defaultCategoryInclusion;
	}

	for(const string &category : onlyShipCategories)
		shipCategories[category] = true;

	for(const string &category : avoidsShipCategories)
		shipCategories[category] = false;
}


int64_t CrewMember::NumberOnShip(const shared_ptr<Ship> &ship, const bool isFlagship, const bool includeExtras) const
{
	// First check if the crew member can occur on this ship at all.
	if(!CanOccurOnShip(ship, isFlagship))
		return 0;

	const string rankingCrewId = isFlagship ? "player" : CrewSetting::RankingCrewId(ship->Attributes().Category());

	// A ship can have only one of the ranking crew member.
	if(rankingCrewId == Id())
		return 1;

	// No other crew members are allowed to outrank the ranking crew member.
	if(WouldOutrankOnShip(ship, rankingCrewId))
		return 0;

	const int64_t countableCrewMembers = includeExtras
		? ship->Crew()
		: ship->RequiredCrew();

	int64_t numberOnShip = 0;

	// Total up the placed crew members within the ship's countable crew
	for(int64_t crewNumber : OccursAt())
		if(crewNumber <= countableCrewMembers)
			++numberOnShip;

	// Prevent division by zero so that the universe doesn't implode.
	if(ShipPopulationPerMember())
	{
		// Figure out how many of this kind of crew we have, by population.
		numberOnShip = max(
			numberOnShip,
			countableCrewMembers / ShipPopulationPerMember()
		);
	}

	return numberOnShip;
}




bool CrewMember::AvoidsEscorts() const
{
	return avoidsEscorts;
}



bool CrewMember::AvoidsFlagship() const
{
	return avoidsFlagship;
}



bool CrewMember::AvoidsParked() const
{
	return avoidsParked;
}



bool CrewMember::OnlyParked() const
{
	return onlyParked;
}



int64_t CrewMember::ParkedSalary() const
{
	if(Preferences::GetParkedShipCrew() == Preferences::ParkedShipCrew::SHARES_ONLY)
		return 0;

	return parkedSalary;
}



int64_t CrewMember::ParkedShares() const
{
	if(Preferences::GetParkedShipCrew() == Preferences::ParkedShipCrew::SALARY_ONLY)
		return 0;

	return parkedShares;
}



int64_t CrewMember::Salary() const
{
	if(Preferences::GetCrewSalaries() != Preferences::CrewSalaries::ON)
		return 0;
	if(Preferences::GetProfitSharing() == Preferences::ProfitSharing::CONVERTED)
	{
		auto salaryPerShare = CrewSetting::SalaryPerShare();
		if(salaryPerShare)
			return salary + shares * salaryPerShare;
		else
			Logger::LogError("Error: Salary per share is zero. Cannot convert shares to salary. Please check crew.txt for errors.");
	}
	return salary;
}



int64_t CrewMember::Shares() const
{
	if(Preferences::GetProfitSharing() != Preferences::ProfitSharing::ON)
		return 0;
	if(Preferences::GetCrewSalaries() == Preferences::CrewSalaries::CONVERTED)
	{
		auto salaryPerShare = CrewSetting::SalaryPerShare();
		if(salaryPerShare)
			return shares + salary / salaryPerShare;
		else
			Logger::LogError("Error: Salary per share is zero. Cannot convert salary to shares. Please check crew.txt for errors.");
	}
	return shares;
}



int64_t CrewMember::SharesPerCombatLevel() const
{
	return sharesPerCombatLevel;
}



int64_t CrewMember::SharesPerLicense() const
{
	return sharesPerLicense;
}



int64_t CrewMember::ShipPopulationPerMember() const
{
	return shipPopulationPerMember;
}



int64_t CrewMember::TotalShares(int combatLevel, int licenseCount) const
{
	return shares + sharesPerCombatLevel * combatLevel + sharesPerLicense * licenseCount;
}



map<const CrewMember*, shared_ptr<CrewMember>> CrewMember::SharedPtrCache;



const shared_ptr<CrewMember> CrewMember::GetSharedPtr() const
{
	auto it = SharedPtrCache.find(this);

	if (it != SharedPtrCache.end())
		return it->second;

	shared_ptr<CrewMember> sharedPtr = make_shared<CrewMember>(*this);
	SharedPtrCache[this] = sharedPtr;
	return sharedPtr;
}



const string CrewMember::Id() const
{
	return id;
}



const string CrewMember::Name() const
{
	return name;
}



const vector<int64_t> CrewMember::OccursAt() const
{
	return occursAt;
}



const bool CrewMember::CanOccurOnShip(const shared_ptr<Ship> ship, const bool isFlagship) const
{
	// The player is accounted for separately and cannot be listed on a ship.
	if(id == "player")
		return false;

	if(
		Preferences::GetRankedCrewMembers() == Preferences::RankedCrewMembers::OFF &&
		(id != "regular" || id != "security")
	)
		return false;

	if(
		Preferences::GetRankedCrewMembers() == Preferences::RankedCrewMembers::MARINES_ONLY &&
		(id != "regular" || id != "marine" || id != "security")
	)
		return false;

	// Prevent crew from appearing on ships that don't have any crew.
	if(ship->Crew() == 0)
		return false;

	// If this is the flagship, check if this crew member avoids the flagship.
	if(isFlagship && avoidsFlagship)
		return false;

	// If this is an escort, check if this crew member avoids escorts.
	if(!isFlagship && avoidsEscorts)
		return false;

	// Filter out crew members that avoid parked ships if this ship is parked.
	if(ship->IsParked() && (avoidsParked || Preferences::GetParkedShipCrew() == Preferences::ParkedShipCrew::OFF))
		return false;

	// Filter out parked-only crew members if this ship is not parked.
	if(!ship->IsParked() && onlyParked)
		return false;

	// Check if the crew member can occur on ships of this category.
	if(!shipCategories.at(ship->Attributes().Category()))
		return false;

	return true;
}



const bool CrewMember::WouldOutrankOnShip(shared_ptr<Ship> ship, const string &rankingCrewId) const
{
	// If no ranking crew ID is provided, use the default for this ship category.
	const string subjectCrewId = rankingCrewId.empty()
		? CrewSetting::RankingCrewId(ship->Attributes().Category())
		: rankingCrewId;

	// Check if this crew member would outrank the ship's ranking crew member.
	for (const string &outrankingCrewId : CrewSetting::OutrankingCrewIds(subjectCrewId))
	{
		if (id == outrankingCrewId)
			return true;
	}

	return false;
}



const std::vector<std::string> CrewMember::AvoidsShipCategories() const
{
	return avoidsShipCategories;
}



const std::vector<std::string> CrewMember::OnlyShipCategories() const
{
	return onlyShipCategories;
}



int64_t CrewMember::DeathBenefit() const
{
	if(
		Preferences::GetDeathPayments() == Preferences::DeathPayments::OFF ||
		Preferences::GetDeathPayments() == Preferences::DeathPayments::SHARES_ONLY
	)
		return 0;

	if(deathBenefit < 0)
	  return Salary() * CrewSetting::DeathBenefitSalaryMultiplier();

  return deathBenefit;
}



int64_t CrewMember::DeathShares() const
{
	if(
		Preferences::GetDeathPayments() == Preferences::DeathPayments::OFF ||
		Preferences::GetDeathPayments() == Preferences::DeathPayments::BENEFITS_ONLY
	)
		return 0;

	if(deathShares < 0)
	  return Shares() * CrewSetting::DeathSharesMultiplier();

  return deathShares;
}



const map<string, bool> CrewMember::ShipCategories() const
{
	return shipCategories;
}
