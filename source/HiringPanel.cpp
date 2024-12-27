/* HiringPanel.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "HiringPanel.h"

#include "Command.h"
#include "Crew.h"
#include "text/Format.h"
#include "GameData.h"
#include "Information.h"
#include "Interface.h"
#include "PlayerInfo.h"
#include "Ship.h"

#include <algorithm>

using namespace std;



HiringPanel::HiringPanel(PlayerInfo &player)
	: player(player), fleetCrewAnalysis(player.FleetCrewAnalysis()), maxHire(0), maxFire(0)
{
	fleetCrewAnalysis = player.FleetCrewAnalysis();
	SetTrapAllEvents(false);
}



void HiringPanel::Step()
{
	fleetCrewAnalysis = player.FleetCrewAnalysis();
	DoHelp("hiring");
}



void HiringPanel::Draw()
{
	const Interface *hiring = GameData::Interfaces().Get("hiring");
	info.ClearConditions();

	maxFire = max(fleetCrewAnalysis->flagshipBunkAnalysis->extraCrew, (int64_t)0);
	maxHire = max((int64_t)0, min(
		fleetCrewAnalysis->flagshipBunkAnalysis->empty,
		fleetCrewAnalysis->fleetBunkAnalysis->empty
	));

	info.SetString("flagship bunks", Format::Number(fleetCrewAnalysis->flagshipBunkAnalysis->total));
	info.SetString("flagship required", Format::Number(fleetCrewAnalysis->flagshipBunkAnalysis->requiredCrew));
	info.SetString("flagship extra", Format::Number(fleetCrewAnalysis->flagshipBunkAnalysis->extraCrew));
	info.SetString("flagship unused", Format::Number(maxHire));

	info.SetString("fleet bunks", Format::Number(fleetCrewAnalysis->fleetBunkAnalysis->total));
	info.SetString("fleet required", Format::Number(fleetCrewAnalysis->fleetBunkAnalysis->requiredCrew));
	info.SetString("fleet unused", Format::Number(fleetCrewAnalysis->fleetBunkAnalysis->empty));
	info.SetString("passengers", Format::Number(fleetCrewAnalysis->fleetBunkAnalysis->passengers));

	info.SetString("salary required", to_string(fleetCrewAnalysis->salaryReport->at(Crew::ReportDimension::Required)));
	info.SetString("shares required", to_string(fleetCrewAnalysis->sharesReport->at(Crew::ReportDimension::Required)));
	info.SetString("salary extra", to_string(fleetCrewAnalysis->salaryReport->at(Crew::ReportDimension::Extra)));
	info.SetString("shares extra", to_string(fleetCrewAnalysis->sharesReport->at(Crew::ReportDimension::Extra)));

	info.SetString("your share of profits", to_string(fleetCrewAnalysis->profitPlayerPercentage) + "%");
	info.SetString("player profit percentage", to_string(fleetCrewAnalysis->profitPlayerPercentage) + "% of fleet profits");
	info.SetString("player daily income", Format::Credits(player.GetDailyGrossIncome()));
	info.SetString("player shares", to_string(fleetCrewAnalysis->playerShares));

	int modifier = Modifier();
	if(modifier > 1)
		info.SetString("modifier", "x " + to_string(modifier));
	else
		info.SetString("modifier", "");

	if(maxHire)
		info.SetCondition("can hire");
	if(maxFire)
		info.SetCondition("can fire");

	hiring->Draw(info, this);
}



bool HiringPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(command.Has(Command::HELP))
	{
		DoHelp("hiring", true);
		return true;
	}

	if(!player.Flagship())
		return false;

	if(key == 'h' || key == SDLK_EQUALS || key == SDLK_KP_PLUS || key == SDLK_PLUS
		|| key == SDLK_RETURN || key == SDLK_SPACE)
	{
		player.Flagship()->AddCrew(min(maxHire, Modifier()));
		player.UpdateCargoCapacities();
	}
	else if(key == 'f' || key == SDLK_MINUS || key == SDLK_KP_MINUS || key == SDLK_BACKSPACE || key == SDLK_DELETE)
	{
		player.Flagship()->AddCrew(-min(maxFire, Modifier()));
		player.UpdateCargoCapacities();
	}
	else
		return false;

	return true;
}
