/*

	Copyright (C) 1991-2001 and beyond by Bungie Studios, Inc.
	and the "Aleph One" developers.
 
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html

*/
/*
 *  network_dummy.cpp - Dummy network functions
 */

#include "cseries.h"
#include "map.h"
#include "network.h"
#include "network_games.h"


void NetExit(void)
{
}

bool NetSync(void)
{
	return true;
}

bool NetUnSync(void)
{
	return true;
}

short NetGetLocalPlayerIndex(void)
{
	return 0;
}

short NetGetPlayerIdentifier(short player_index)
{
	return 0;
}

short NetGetNumberOfPlayers(void)
{
	return 1;
}

void *NetGetPlayerData(short player_index)
{
	return NULL;
}

void *NetGetGameData(void)
{
	return NULL;
}

bool NetChangeMap(struct entry_point *entry)
{
	return false;
}

int32 NetGetNetTime(void)
{
	return 0;
}

void display_net_game_stats(void)
{
}

bool network_gather(void)
{
	return false;
}

int network_join(void)
{
	return false;
}

bool current_game_has_balls(void)
{
	return false;
}

bool NetAllowBehindview(void)
{
	return false;
}

bool NetAllowCrosshair(void)
{
	return false;
}

bool NetAllowTunnelVision(void)
{
	return false;
}

// Web port (see ../../WEB_PORT_PLAN.md, M3b-v): network.h/network_games.h
// grew a lot of declarations since this dummy file was last touched, and
// DISABLE_NETWORKING had never actually been wired up to config.h before
// (see WEB_PORT_PLAN.md) -- so none of this had ever actually been linked.
// These follow the existing style above: harmless no-op/empty defaults.

int32 team_netgame_parameters[NUMBER_OF_TEAM_COLORS][2] = {};

long get_player_net_ranking(short player_index, short *kills, short *deaths, bool game_is_over)
{
	if (kills) *kills = 0;
	if (deaths) *deaths = 0;
	return 0;
}

void calculate_player_rankings(struct player_ranking_data *rankings)
{
}

void calculate_ranking_text(char *buffer, long ranking)
{
	if (buffer) buffer[0] = '\0';
}

short get_network_compass_state(short player_index)
{
	return _network_compass_all_off;
}

std::string NetSessionIdentifier(void)
{
	return std::string();
}

void match_starts_with_existing_players(struct player_start_data* ioStartArray, short* ioStartCount)
{
}

bool NetAllowOverlayMap()
{
	return false;
}

bool NetAllowSavingLevel()
{
	return true;
}

int32 NetGetUnconfirmedActionFlagsCount()
{
	return 0;
}

uint32 NetGetUnconfirmedActionFlag(int32 offset)
{
	return 0;
}

void NetUpdateUnconfirmedActionFlags()
{
}

int32 NetGetLatency()
{
	return NetworkStats::invalid;
}

const NetworkStats& NetGetStats(int player_index)
{
	static const NetworkStats sInvalidStats = { NetworkStats::invalid, NetworkStats::invalid, NetworkStats::invalid, 0 };
	return sInvalidStats;
}

bool NetCheckWorldUpdate()
{
	return true;
}

int32& hub_get_minimum_send_period()
{
	static int32 minimum_send_period = 0;
	return minimum_send_period;
}

void hub_set_minimum_send_period(int32 new_minimum)
{
}
