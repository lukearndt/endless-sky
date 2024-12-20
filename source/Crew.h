/* Crew.h
Copyright (c) 2024 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

#ifndef CREW_H_
#define CREW_H_

#include "CrewMember.h"
#include "DataNode.h"
#include "Ship.h"

class Crew
{
public:
	// These types exist to improve the readability of the code.
	using Count = int64_t;
	using Total = int64_t;

	// A vector of shared pointers to all of the player's ships
	using Fleet = std::vector<std::shared_ptr<Ship>>;

	// A map of shared pointers to crew members, and how many are present.
	// Does not contain the player.
	using Manifest = std::map<const std::shared_ptr<CrewMember>, Count>;

	enum ReportDimension
	{
		Actual = 0,
		Required = 1,
		Extra = 2
	};

	/**
	* A vector containing three variants of a subject:
	*
	*	0: actual
	*	1: required
	*	2: extra
	*
	* These three variants are formalised in the ReportDimension enum.
	*
	* We need to report in this way so that we can determine the impact of
	* hiring extra crew members and display that to the player.
	*/
	template <typename T>
	using Report = std::vector<T>;
	using SummaryEntry = std::tuple<std::string, Count, Total, Total>;

	class BunkAnalysis {
		public:
			BunkAnalysis(const std::shared_ptr<Ship> &ship);
			BunkAnalysis(const Ship * shipPtr);
			BunkAnalysis();

			Count total;
			Count requiredCrew;
			Count extraCrew;
			Count passengers;
			Count occupied;
			Count empty;
	};

	class ShipAnalysis {
		public:
			ShipAnalysis(const std::shared_ptr<Ship> &ship, const bool isFlagship);

			const std::shared_ptr<Ship> ship;
			const bool isFlagship;
			const std::shared_ptr<BunkAnalysis> bunkAnalysis;
			const std::shared_ptr<CrewMember> rankingCrewMember;
			std::shared_ptr<Report<std::shared_ptr<Manifest>>> manifestReport;
			std::shared_ptr<Report<Count>> crewCountReport;
			Total deathBenefits;
			Total deathShares;
			std::shared_ptr<Report<Total>> salaryReport;
			std::shared_ptr<Report<Total>> sharesReport;
			std::shared_ptr<std::vector<SummaryEntry>> CrewSummary();

		private:
			bool crewSummaryReady = false;
			std::shared_ptr<std::vector<SummaryEntry>> crewSummary;
	};

	class FleetAnalysis {
		public:
			FleetAnalysis(const Fleet& subjectFleet, const Ship *flagshipPtr, int combatLevel, int licenseCount);

			std::shared_ptr<BunkAnalysis> fleetBunkAnalysis;
			const std::shared_ptr<BunkAnalysis> flagshipBunkAnalysis;
			const std::shared_ptr<Fleet> fleet;
			const Total playerShares;
			double profitShareRatio;
			std::shared_ptr<Report<Count>> crewCountReport;
			std::shared_ptr<Report<std::shared_ptr<Manifest>>> manifestReport;
			std::shared_ptr<Report<Total>> salaryReport;
			std::shared_ptr<Report<Total>> sharesReport;
			std::shared_ptr<std::vector<std::shared_ptr<ShipAnalysis>>> shipAnalyses;
			Total deathBenefits;
			Total deathShares;
			Total nonPlayerShares;
			Total fleetSharesIncludingPlayer;
			Total profitPlayerPercentage;
	};

	class CasualtyAnalysis {
		public:
			CasualtyAnalysis(const ShipAnalysis &shipAnalysisBefore, const std::shared_ptr<Ship> &shipAfter);

			std::shared_ptr<Manifest> manifestAfter;
			std::shared_ptr<Manifest> casualtyManifest;
			Count casualtyCount;
			Total deathBenefits;
			Total deathShares;
	};

	// Create a Crew::Manifest for the required crew members of a ship
	static std::shared_ptr<Manifest> BuildRequiredCrewManifest(const std::shared_ptr<Ship> &ship, const bool isFlagship = false);

	// Create a Crew::ManifestReport for a single ship.
	static std::shared_ptr<Report<std::shared_ptr<Manifest>>> BuildManifestReport(const std::shared_ptr<Ship> &ship, const bool isFlagship = false);

	// Generate a manifest of the crew members that are in manifest a manifest but not in manifest b
	static std::shared_ptr<Manifest> ManifestDifference(
		const std::shared_ptr<Manifest> &a,
		const std::shared_ptr<Manifest> &b
	);

	// Mutate the target BunkAnalysis by adding each value in the source
	// BunkAnalysis to the corresponding value in the target.
	static std::shared_ptr<BunkAnalysis> MergeBunkAnalyses(
		std::shared_ptr<BunkAnalysis> const &target,
		const std::shared_ptr<BunkAnalysis> &source
	);

	// Mutate the target Report by adding each int64_t in the source
	// Report to the corresponding int64_t in the target.
	static std::shared_ptr<Report<int64_t>> MergeReports(
		std::shared_ptr<Report<int64_t>> const &target,
		const std::shared_ptr<Report<int64_t>> &source
	);

	// Mutate the target Report by adding the crew count from each Manifest
	// in the source Report to the corresponding crew count in the target
	static std::shared_ptr<Report<std::shared_ptr<Manifest>>> MergeReports(
		std::shared_ptr<Report<std::shared_ptr<Manifest>>> const &target,
		const std::shared_ptr<Report<std::shared_ptr<Manifest>>> &source
	);
};

#endif
