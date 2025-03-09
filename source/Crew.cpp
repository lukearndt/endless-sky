/* Crew.cpp
Copyright (c) 2024 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

#include "Crew.h"

#include "CategoryList.h"
#include "CrewMember.h"
#include "CrewSetting.h"
#include "GameData.h"
#include "Logger.h"
#include "PlayerInfo.h"
#include "Preferences.h"

#include <algorithm>

using namespace std;


/**
 * Constructor for the BunkAnalysis class.
 * This variant of the constructor is used to analyse a shared_ptr<Ship>.
 *
 * @param ship A shared pointer to the ship that you want to analyse.
 * @return A Crew::BunkAnalysis object containing an analysis of the ship.
 */
Crew::BunkAnalysis::BunkAnalysis(const std::shared_ptr<Ship> &ship) :
	total(ship->Attributes().Get("bunks")),
	requiredCrew(ship->RequiredCrew()),
	extraCrew(ship->Crew() - requiredCrew),
	passengers(ship->Cargo().Passengers()),
	occupied(ship->Crew() + passengers),
	empty(total - occupied) {};



/**
 * Constructor for the BunkAnalysis class.
 * This variant of the constructor is used to analyse a Ship pointer.
 *
 * @param ship A shared pointer to the ship that you want to analyse.
 * @return A Crew::BunkAnalysis object containing an analysis of the ship.
 */
Crew::BunkAnalysis::BunkAnalysis(const Ship * ship) :
	total(ship->Attributes().Get("bunks")),
	requiredCrew(ship->RequiredCrew()),
	extraCrew(ship->Crew() - requiredCrew),
	passengers(ship->Cargo().Passengers()),
	occupied(ship->Crew() + passengers),
	empty(total - occupied) {};



/**
 * Constructor for the BunkAnalysis class.
 * This variant of the constructor is used to create an empty analysis so
 * that we can populate it manually. We do this when analyzing a fleet.
 *
 * @return An empty Crew::BunkAnalysis object.
 */
Crew::BunkAnalysis::BunkAnalysis() :
	total(0),
	requiredCrew(0),
	extraCrew(0),
	passengers(0),
	occupied(0),
	empty(0) {};



/**
* Constructor for the ShipAnalysis class.
* Analyses the crew and passenger details of a given ship.
*
* @param subjectShip A shared pointer to the ship being analyzed.
* @param subjectIsFlagship A boolean indicating if the ship is the flagship.
* @return A Crew::ShipAnalysis object containing an analysis of the ship.
*
* Initializes the following member variables:
* - ship: The ship being analyzed.
* - isFlagship: Whether or not the ship is the flagship.
* - bunkAnalysis: An analysis of the ship's bunks.
* - rankingCrewMember: The crew member that commands the ship.
* - manifestReport: A Crew::Report of Crew::Manifest objects.
* - crewCountReport: A Crew::Report of the crew count.
* - deathBenefits: The total amount owed in death benefits.
			This is 0 if the ship has not been destroyed.
* - deathShares: The total amount owed in death shares.
			This is 0 if the ship has not been destroyed.
* - salaryReport: A Crew::Report of the crew salaries.
* - sharesReport: A Crew::Report of the crew shares.
*/
Crew::ShipAnalysis::ShipAnalysis(const std::shared_ptr<Ship> &subjectShip, const bool subjectIsFlagship)
	: ship(subjectShip),
		isFlagship(subjectIsFlagship),
		bunkAnalysis(make_shared<BunkAnalysis>(subjectShip)),
		rankingCrewMember(
			GameData::CrewMembers().Get(
				CrewSetting::RankingCrewId(subjectShip->Attributes().Category())
			)->GetSharedPtr()
		),
		manifestReport(BuildManifestReport(ship, isFlagship)),
		crewCountReport(make_shared<Report<Count>>(initializer_list<Count>{0, 0, 0})),
		deathBenefits(0),
		deathShares(0),
		salaryReport(make_shared<Report<Total>>(initializer_list<Total>{0, 0, 0})),
		sharesReport(make_shared<Report<Total>>(initializer_list<Total>{0, 0, 0}))
{
	// If the ship is destroyed, calculate only the death benefits
	// and death shares since nothing else is going to matter.
	if(ship->IsDestroyed()) {
		for (const auto &crewMemberEntry : *manifestReport->at(0))
		{
			deathBenefits += crewMemberEntry.first->DeathBenefit() * crewMemberEntry.second;
			deathShares += crewMemberEntry.first->DeathShares() * crewMemberEntry.second;
		}
		return;
	}

	// When the ship has no extra crew, we can take some shortcuts.
	// This is normally the case for escort ships, so it's worth
	// optimising the performance of this branch as much as we can.
	//
	// A side effect of this is that a ship with insufficient crew will
	// require salary and shares as if it has all of its required crew
	// members. This is probably okay because we can justify it as the
	// crew receiving a performance bonus to reward them for the situation.
	if(manifestReport->at(ReportDimension::Extra)->empty())
	{
		for (const auto &crewMemberEntry : *manifestReport->at(ReportDimension::Required))
		{
			crewCountReport->at(ReportDimension::Required) += crewMemberEntry.second;

			// Add the salary and shares to the report.
			if(ship->IsParked())
			{
				salaryReport->at(ReportDimension::Required) += crewMemberEntry.first->ParkedSalary() * crewMemberEntry.second;
				sharesReport->at(ReportDimension::Required) += crewMemberEntry.first->ParkedShares() * crewMemberEntry.second;
			}
			else
			{
				salaryReport->at(ReportDimension::Required) += crewMemberEntry.first->Salary() * crewMemberEntry.second;
				sharesReport->at(ReportDimension::Required) += crewMemberEntry.first->Shares() * crewMemberEntry.second;
			}
		}
		crewCountReport->at(ReportDimension::Actual) = crewCountReport->at(ReportDimension::Required);
		salaryReport->at(ReportDimension::Actual) = salaryReport->at(ReportDimension::Required);
		sharesReport->at(ReportDimension::Actual) = sharesReport->at(ReportDimension::Required);

		return;
	}

	// If the ship has extra crew, we need to be more thorough.
	// This is normally only the case for the flagship, so it's okay that
	// the process is a bit more performance hungry.
	for(int reportDimension = 0; reportDimension < 3; ++reportDimension)
	{
		for (const auto &crewMemberEntry : *manifestReport->at(reportDimension))
		{
			crewCountReport->at(reportDimension) += crewMemberEntry.second;

			// Add the salary and shares to the report.
			if(ship->IsParked())
			{
				salaryReport->at(reportDimension) += crewMemberEntry.first->ParkedSalary() * crewMemberEntry.second;
				sharesReport->at(reportDimension) += crewMemberEntry.first->ParkedShares() * crewMemberEntry.second;
			}
			else
			{
				salaryReport->at(reportDimension) += crewMemberEntry.first->Salary() * crewMemberEntry.second;
				sharesReport->at(reportDimension) += crewMemberEntry.first->Shares() * crewMemberEntry.second;
			}
		}
	}
}


/**
 * Builds a vector of Crew::SummaryEntry objects based on the
 * ship's actual crew manifest. This is used to build a summary
 * for display in the shipyard and ship info panel.
 *
 * Caches the result after the first call to prevent it from
 * being recalculated multiple times, since the ShipAnalysis is
 * not expected to change after construction.
 *
 * If the Preferences::RankedCrewMembers setting is not ON, returns an
 * empty vector to prevent the summary from being displayed.
 *
 * @return A vector of Crew::SummaryEntry objects.
 */
shared_ptr<vector<Crew::SummaryEntry>> Crew::ShipAnalysis::CrewSummary()
{
	if(crewSummaryReady)
		return crewSummary;

	auto summary = make_shared<vector<SummaryEntry>>();

	bool isParked = ship->IsParked();

	if(Preferences::GetRankedCrewMembers() == Preferences::RankedCrewMembers::ON)
	{
		for(const auto &crewMemberEntry : *this->manifestReport->at(Crew::ReportDimension::Actual))
		{
			shared_ptr<CrewMember> crewMember = crewMemberEntry.first;

			Count count = crewMemberEntry.second;

			summary->push_back({
				crewMember->Name(),
				count,
				isParked ? crewMember->ParkedSalary() : crewMember->Salary(),
				isParked ? crewMember->ParkedShares() : crewMember->Shares()
			});
		}

		if(!summary->empty())
		{
			// Sort by salary, and if that's the same, by shares.
			sort(
				summary->begin(), summary->end(),
				[](const SummaryEntry &a, const SummaryEntry &b
			) {
				if (get<2>(a) != get<2>(b))
					return get<2>(a) > get<2>(b);
				return get<3>(a) > get<3>(b);
			});
		}
	}

	crewSummary = summary;
	crewSummaryReady = true;

	return crewSummary;
}



/**
* Constructor for the FleetAnalysis class.
* Analyses the crew and passenger details of a given fleet.
*
* @param subjectFleet A vector of shared pointers to the ships in the fleet.
* @param flagshipPtr A pointer to the flagship of the fleet.
* @param licenseCount The number of licenses that the player has.
* @return A Crew::FleetAnalysis object containing an analysis of the ship.
*/
Crew::FleetAnalysis::FleetAnalysis(
	const Fleet &subjectFleet,
	const Ship * flagshipPtr,
	int combatLevel,
	int creditScore,
	int licenseCount,
	int passengers
)	:
	fleetBunkAnalysis(make_shared<BunkAnalysis>()),
	flagshipBunkAnalysis(make_shared<BunkAnalysis>(flagshipPtr)),
	playerShares(PlayerShares(combatLevel, creditScore, licenseCount)),
	profitShareRatio(0),
	crewCountReport(make_shared<Report<Count>>(initializer_list<Count>{0, 0, 0})),
	manifestReport(make_shared<Report<shared_ptr<Manifest>>>(initializer_list<shared_ptr<Manifest>>{
		make_shared<Manifest>(),
		make_shared<Manifest>(),
		make_shared<Manifest>(),
	})),
	salaryReport(make_shared<Report<Total>>(initializer_list<Total>{0, 0, 0})),
	sharesReport(make_shared<Report<Total>>(initializer_list<Total>{0, 0, 0})),
	shipAnalyses(make_shared<vector<shared_ptr<ShipAnalysis>>>()),
	deathBenefits(0),
	deathShares(0),
	fleetSharesIncludingPlayer(0),
	profitPlayerPercentage(100)
{
	Manifest manifestExtra;
	Manifest manifestRequired;
	Manifest manifestTotal;

	// Analyse each ship in the fleet and merge the results as we go.
	for (const shared_ptr<Ship> &ship : subjectFleet)
	{
		ShipAnalysis shipAnalysis(ship, ship.get() == flagshipPtr);
		shipAnalyses->push_back(make_shared<ShipAnalysis>(shipAnalysis));

		// If the ship has been destroyed, we can simplify the analysis.
		if(ship->IsDestroyed())
		{
			deathBenefits += shipAnalysis.deathBenefits;
			deathShares += shipAnalysis.deathShares;
		}
		else
		{
			// Parked ships can't carry passengers or extra crew members,
			// so they aren't included in the fleet's overall bunk analysis.
			if(!ship->IsParked())
				MergeBunkAnalyses(fleetBunkAnalysis, shipAnalysis.bunkAnalysis);
			MergeReports(crewCountReport, shipAnalysis.crewCountReport);
			MergeReports(salaryReport, shipAnalysis.salaryReport);
			MergeReports(sharesReport, shipAnalysis.sharesReport);
			MergeReports(manifestReport, shipAnalysis.manifestReport);
		}
	}

	// Passengers are managed by the cargo system, which pools everything
	// together while we're on a planet, so we can't rely on the passenger
	// count from each ship to be accurate.
	fleetBunkAnalysis->passengers = passengers;
	fleetBunkAnalysis->occupied =
		fleetBunkAnalysis->requiredCrew
		+ fleetBunkAnalysis->extraCrew
		+ passengers;
	fleetBunkAnalysis->empty =
		fleetBunkAnalysis->total
		- fleetBunkAnalysis->occupied;

	// Calculate the fleet's profit sharing requirements.
	nonPlayerShares = sharesReport->at(ReportDimension::Actual) + deathShares;
	fleetSharesIncludingPlayer = nonPlayerShares + playerShares;

	if (fleetSharesIncludingPlayer > 0)
	{
		profitShareRatio = double(nonPlayerShares) / double(fleetSharesIncludingPlayer);
		profitPlayerPercentage = playerShares * 100 / fleetSharesIncludingPlayer;
	}
	else
		Logger::LogError("Crew::FleetAnalysis - Profit sharing disabled because the fleet has no shares; check for problems in data/crew.txt");
}



/**
 * Backup constructor for the FleetAnalysis class.
 * This constructor is only used when the fleet has no ships,
 * or when it somehow lacks a flagship.
 *
 * @param combatLevel The player's combat level.
 * @param creditScore The player's credit score.
 * @param licenseCount The number of licenses that the player has.
 */
Crew::FleetAnalysis::FleetAnalysis(int combatLevel, int creditScore, int licenseCount) :
	fleetBunkAnalysis(make_shared<BunkAnalysis>()),
	flagshipBunkAnalysis(make_shared<BunkAnalysis>()),
	playerShares(PlayerShares(combatLevel, creditScore, licenseCount)),
	profitShareRatio(0),
	crewCountReport(make_shared<Report<Count>>(initializer_list<Count>{0, 0, 0})),
	manifestReport(make_shared<Report<shared_ptr<Manifest>>>(initializer_list<shared_ptr<Manifest>>{
		make_shared<Manifest>(),
		make_shared<Manifest>(),
		make_shared<Manifest>(),
	})),
	salaryReport(make_shared<Report<Total>>(initializer_list<Total>{0, 0, 0})),
	sharesReport(make_shared<Report<Total>>(initializer_list<Total>{0, 0, 0})),
	shipAnalyses(make_shared<vector<shared_ptr<ShipAnalysis>>>()),
	deathBenefits(0),
	deathShares(0),
	fleetSharesIncludingPlayer(playerShares),
	profitPlayerPercentage(100)
{}



/**
 * Constructor for the CasualtyAnalysis class.
 * Analyses the crew members lost based on its manifest before and after a change.
 * This is used to calculate death benefits and death shares for the lost crew.
 * If the ship is destroyed, the manifestAfter will be empty and the casualtyManifest
 * will be the same as the Actual manifest from shipAnalysisBefore.
 *
 * @param shipAnalysisBefore A ShipAnalysis object representing the ship before the change.
 * @param shipAfter A shared pointer to the ship after the change.
 * @return A Crew::CasualtyAnalysis object containing an analysis of the lost crew.
 */
Crew::CasualtyAnalysis::CasualtyAnalysis(const shared_ptr<ShipAnalysis> &shipAnalysisBefore, const shared_ptr<Ship> &shipAfter) :
	manifestAfter(
		shipAfter->IsDestroyed()
		?	make_shared<Manifest>()
		:	BuildManifestReport(shipAfter, shipAnalysisBefore->isFlagship)->at(ReportDimension::Actual)
	),
	casualtyManifest(
		shipAfter->IsDestroyed()
		?	shipAnalysisBefore->manifestReport->at(ReportDimension::Actual)
		: ManifestDifference(shipAnalysisBefore->manifestReport->at(ReportDimension::Actual), manifestAfter)
	),
	casualtyCount(0),
	deathBenefits(0),
	deathShares(0)
{
	for (const auto &crewMemberEntry : *casualtyManifest)
	{
		casualtyCount += crewMemberEntry.second;
		deathBenefits += crewMemberEntry.first->DeathBenefit() * crewMemberEntry.second;
		deathShares += crewMemberEntry.first->DeathShares() * crewMemberEntry.second;
	}
}



/**
 * Generates a manifest of the required crew members aboard a given ship.
 *
 * For an escort ship, this is equivalent to the total crew manifest
 * and can be used directly.
 *
 * For the flagship, use the BuildFlagshipManifests function to get
 * a more comprehensive breakdown of the crew.
 *
 * @param ship A shared pointer to the ship for which the crew manifest is being generated.
 * @param isFlagship A boolean indicating if the ship is the flagship.
 *   This allows the function to be used by BuildFlagshipManifests
 *   when the flagship does not have any extra crew members.
 * @return A Crew::Manifest that maps a shared pointer to each
 *   required CrewMember against how many of them the ship needs.
 */
shared_ptr<Crew::Manifest> Crew::BuildRequiredCrewManifest(const shared_ptr<Ship> &ship, const bool isFlagship)
{
	// Map of each crew ID to the number abord the ship
	Manifest manifest;

	// Check that we have crew data before proceeding
	if(GameData::CrewMembers().size() < 1)
	{
		Logger::LogError("Error: could not find any crew member definitions in the data files.");
		return make_shared<Manifest>(manifest);
	}

	int64_t crewAccountedFor = 0;

	for(const auto &crewMemberEntry : GameData::CrewMembers())
	{
		const shared_ptr<CrewMember> crewMember = crewMemberEntry.second.GetSharedPtr();

		int numberOnShip = crewMember->NumberOnShip(ship,	isFlagship, false);

		if(numberOnShip)
			manifest[crewMember] = numberOnShip;

		crewAccountedFor += numberOnShip;
	}

	int64_t remainingCrewMembers = ship->RequiredCrew() - crewAccountedFor
		// The flagship has one fewer crew member because the player is on it.
		- isFlagship;

	// Any remaining crew members are regulars if they can occur on the ship
	if(remainingCrewMembers > 0)
	{
		const shared_ptr<CrewMember> regular = GameData::CrewMembers().Get("regular")->GetSharedPtr();

		// If regular crew members can't occur on the ship, it might have a
		// lower crew count than the required crew count. This usually happens
		// when the ship is parked.
		if(regular->CanOccurOnShip(ship, isFlagship))
			manifest[regular] = remainingCrewMembers;
	}

	return make_shared<Manifest>(manifest);
}




/**
 * Generates a manifest of the crew members aboard a given ship.
 *
 * @param ship A shared pointer to the ship that we're reporting on.
 * @param isFlagship A boolean indicating if the ship is the flagship.
 * @return A Crew::ManifestReport containing manifests for the
 * actual, required, and extra crew.
 */
shared_ptr<Crew::Report<shared_ptr<Crew::Manifest>>> Crew::BuildManifestReport(
	const shared_ptr<Ship> &ship,
	const bool isFlagship
)
{
	Manifest actualManifest;
	Manifest requiredManifest;
	Manifest extraManifest;

	if(ship->Crew() == ship->RequiredCrew())
	{
		const shared_ptr<Manifest> manifest = make_shared<Manifest>(*BuildRequiredCrewManifest(ship, isFlagship));
		auto report = Report<shared_ptr<Manifest>>{manifest, manifest, make_shared<Manifest>(extraManifest)};
		return make_shared<Report<shared_ptr<Manifest>>>(report);
	}

	// Check that we have crew data before proceeding
	if(GameData::CrewMembers().size() < 1)
	{
		Logger::LogError("Error: could not find any crew member definitions in the data files.");

		// Return three empty crew manifests
		const shared_ptr<Manifest> emptyManifest = make_shared<Manifest>(actualManifest);
		auto report = Report<shared_ptr<Crew::Manifest>>{emptyManifest, emptyManifest, emptyManifest};
		return make_shared<Report<shared_ptr<Crew::Manifest>>>(report);
	}

	int64_t actualCrewAccountedFor = 0;
	int64_t requiredCrewAccountedFor = 0;

	for(auto &crewMemberEntry : GameData::CrewMembers())
	{
		const shared_ptr<CrewMember> crewMember = crewMemberEntry.second.GetSharedPtr();

		int64_t actualOnShip = crewMember->NumberOnShip(ship, isFlagship, true);
		int64_t requiredOnShip = crewMember->NumberOnShip(ship, isFlagship, false);

		if(actualOnShip)
			actualManifest[crewMember] = actualOnShip;

		if(requiredOnShip)
			requiredManifest[crewMember] = requiredOnShip;

		actualCrewAccountedFor += actualOnShip;
		requiredCrewAccountedFor += requiredOnShip;
	}

	// Figure out how many crew members we still need to account for
	int64_t remainingActualCrewMembers = ship->Crew() - actualCrewAccountedFor - 1;
	int64_t remainingRequiredCrewMembers = ship->RequiredCrew() - requiredCrewAccountedFor - 1;

	const shared_ptr<CrewMember> regular = GameData::CrewMembers().Get("regular")->GetSharedPtr();

	// Any remaining required crew members are regulars
	int64_t regulars = min(remainingActualCrewMembers, remainingRequiredCrewMembers);

	if(regulars > 0)
	{
		actualManifest[regular] = regulars;
		requiredManifest[regular] = regulars;
	}

	// Any additional actual crew members are marines hired for boarding actions
	int64_t marines = remainingActualCrewMembers - regulars;

	if(marines > 0)
	{
		const shared_ptr<CrewMember> marine = GameData::CrewMembers().Get("marine")->GetSharedPtr();
		actualManifest[marine] = marines;
	}

	shared_ptr<Manifest> actualManifestPtr = make_shared<Manifest>(actualManifest);
	shared_ptr<Manifest> requiredManifestPtr = make_shared<Manifest>(requiredManifest);

	auto report = Report<shared_ptr<Crew::Manifest>>{
		actualManifestPtr,
		requiredManifestPtr,
		ManifestDifference(actualManifestPtr, requiredManifestPtr)
	};

	return make_shared<Report<shared_ptr<Crew::Manifest>>>(report);
}



/**
 * Estimates the average cost of a crew member dying.
 *
 * Can be used to evaluate whether or not it will be financially worth
 * the risk of some crew members dying to achieve an objective,
 * such as capturing a ship.
 *
 * This is a somewhat flawed estimate because it doesn't account for all
 * of the factors involved.
 *
 * It can't know how much profit will be made that day, so the credits
 * lost to death shares are a limited guess at best.
 *
 * It also doesn't know the actual ranks of the crew members that will
 * die, so the actual cost may be worse if any high ranked crew members
 * are killed instead of marines or regulars. To help mitigate this, it
 * takes the worst case scenario between those two types of crew members.
 *
 * @param hasExtraCrew A boolean indicating if the ship has extra crew members.
 *
 * @return The possible financial cost of a crew member dying.
 */
int64_t Crew::ExpectedCostPerCasualty(bool hasExtraCrew)
{
	int64_t expectedDeathBenefit = max(
		GameData::CrewMembers().Get("regular")->DeathBenefit(),
		hasExtraCrew ? GameData::CrewMembers().Get("marine")->DeathBenefit() : 0
	);

	int64_t expectedDeathShares = max(
		GameData::CrewMembers().Get("regular")->DeathShares(),
		hasExtraCrew ? GameData::CrewMembers().Get("marine")->DeathShares() : 0
	);

	return expectedDeathBenefit
		+ expectedDeathShares * CrewSetting::SalaryPerShare() * CrewSetting::DeathBenefitSalaryMultiplier();
}



/**
 * Generate a manifest of the crew members that are in manifest a but not
 * in manifest b.
 *
 * @param a A Crew::Manifest, such as of the actual crew members.
 * @param b A Crew::Manifest, such as of the required crew members.
 * @return A Crew::Manifest of the crew members that are in a,
 * but not in b. This can be used to figure out extra crew members,
 * or make a list of casualties after a boarding action.
 */
shared_ptr<Crew::Manifest> Crew::ManifestDifference(
    const shared_ptr<Crew::Manifest> &a,
    const shared_ptr<Crew::Manifest> &b)
{
	shared_ptr<Manifest> manifest = make_shared<Manifest>();

	for (const auto &aCrewEntry : *a)
	{
		auto requiredIt = b->find(aCrewEntry.first);

		const int64_t difference = aCrewEntry.second - (
			requiredIt != b->end() ?
			requiredIt->second
			: 0
		);

		if (difference > 0)
		{
			manifest->insert(aCrewEntry);
			manifest->at(aCrewEntry.first) = difference;
		}
	}

	return manifest;
}



/**
 * Mutate the target report of int64_t values by adding each value in the
 * source report to the corresponding value in the target report.
 *
 * Does not mutate the source report.
 *
 * @param target The report to merge into. This will be mutated.
 * @param source The report to merge from. This will not be mutated.
 *
 * @return The merged report.
 */
shared_ptr<Crew::BunkAnalysis> Crew::MergeBunkAnalyses(
	shared_ptr<BunkAnalysis> const &target,
	const shared_ptr<BunkAnalysis> &source
)
{
	target->total += source->total;
	target->requiredCrew += source->requiredCrew;
	target->extraCrew += source->extraCrew;
	target->passengers += source->passengers;
	target->occupied += source->occupied;
	target->empty += source->empty;

	return target;
}



/**
 * Mutate the target report of int64_t values by adding each value in the
 * source report to the corresponding value in the target report.
 *
 * Does not mutate the source report.
 *
 * @param target The report to merge into. This will be mutated.
 * @param source The report to merge from. This will not be mutated.
 *
 * @return The merged report.
 */
shared_ptr<Crew::Report<int64_t>> Crew::MergeReports(
	shared_ptr<Report<int64_t>> const &target,
	const shared_ptr<Report<int64_t>> &source)
{
	for (int reportDimension = 0; reportDimension < target->size(); ++reportDimension)
		target->at(reportDimension) += source->at(reportDimension);

	return target;
}



/**
 * Mutate the target report of shared pointers to Crew::Manifests
 * by adding the count of each crew member in the source report to
 * the corresponding count in the target report.
 *
 * Does not mutate the source report.
 *
 * @param target The report to merge into, mutating its Manifests.
 * @param source The report to merge from. This will not be mutated.
 *
 * @return The merged report.
 */
shared_ptr<Crew::Report<shared_ptr<Crew::Manifest>>> Crew::MergeReports(
	shared_ptr<Crew::Report<shared_ptr<Crew::Manifest>>> const &target,
	const shared_ptr<Crew::Report<shared_ptr<Crew::Manifest>>> &source
)
{
	// If the source has no extra crew, we can take some shortcuts.
	// Since this is normally the case for escort ships, it's the most
	// likely execution path and worth some performance optimisation.
	if(source->at(ReportDimension::Extra)->size() == 0)
	{
		for(const auto &actualCrewEntry : *source->at(ReportDimension::Actual))
		{
			auto it = target->at(ReportDimension::Actual)->find(actualCrewEntry.first);

			if(it != target->at(ReportDimension::Actual)->end())
				it->second += actualCrewEntry.second;
			else
				target->at(ReportDimension::Actual)->insert(actualCrewEntry);

			it = target->at(ReportDimension::Required)->find(actualCrewEntry.first);

			if(it != target->at(ReportDimension::Required)->end())
				it->second += actualCrewEntry.second;
			else
				target->at(ReportDimension::Required)->insert(actualCrewEntry);
		}
	}
	else // We need to be more thorough if the source has extra crew.
	{
		for (int reportDimension = 0; reportDimension < 3; ++reportDimension)
		{
			for(const auto &crewEntry : *source->at(reportDimension))
			{
				auto it = target->at(reportDimension)->find(crewEntry.first);

				if(it != target->at(reportDimension)->end())
					it->second += crewEntry.second;
				else
					target->at(reportDimension)->insert(crewEntry);
			}
		}
	}

	return target;
}



/**
 * Calculate the player's total number of shares in the
 * fleet's profits and losses.
 *
 * @param combatLevel The player's combat level.
 * @param creditScore The player's credit score.
 * @param licenseCount The number of licenses that the player has earned.
 *
 * @return The player's current shares in the fleet.
*/
int64_t Crew::PlayerShares(const int combatLevel, const int creditScore, const int licenseCount)
{
	int64_t playerShares = CrewSetting::PlayerSharesBase()
		+ CrewSetting::PlayerSharesPerCombatLevel() * combatLevel
		+ CrewSetting::PlayerSharesPerCreditRating() * creditScore
		+ CrewSetting::PlayerSharesPerLicense() * licenseCount;

  return max(playerShares, CrewSetting::PlayerSharesMinimum());
}
