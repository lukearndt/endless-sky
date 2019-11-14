/* Crew.cpp
Copyright (c) 2019 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include "Crew.h"
#include "GameData.h"

int64_t Crew::CalculateSalaries(const Ship *flagship, const vector<shared_ptr<Ship>> ships)
{
	int64_t juniorOfficers = 0;
	int64_t seniorOfficers = 0;
	int64_t pilots = 0;
	int64_t totalCrew = 0;
	int64_t totalSalaries = 0;

	for(const shared_ptr<Ship> &ship : ships)
		if(!ship->IsDestroyed()) {
			// Every ship needs a pilot.
			pilots += 1;
			// We need juniorOfficers to manage our regular crew.
			// If we ever support hiring more crew for escorts, we should use ship->Crew() for these.
			juniorOfficers += ship->RequiredCrew() / GameData::Crew().Get("Crew Per Junior Officer").Value();
			// This is easier than omitting seniorOfficers and juniorOfficers as we go.
			totalCrew += ship->RequiredCrew();
		}

	// Add any extra crew from the flagship.
	if(flagship)
		totalCrew += flagship->Crew() - flagship->RequiredCrew();

	// We don't need a commander for the flagship. We command it directly.
	totalSalaries += (seniorOfficers - 1) * CREDITS_PER_COMMANDER;

	totalSalaries += juniorOfficers * CREDITS_PER_OFFICER;

	// seniorOfficers and juniorOfficers are not regular crew members.
	totalSalaries += (totalCrew - seniorOfficers - juniorOfficers) * CREDITS_PER_REGULAR;

	return totalSalaries;
}

const string &Crew::Name() const
{
	return name;
}

const int64_t &Crew::Value() const
{
	return value;
}
