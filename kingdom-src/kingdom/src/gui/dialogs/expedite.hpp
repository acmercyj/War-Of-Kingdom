/* $Id: player_selection.hpp 49605 2011-05-22 17:56:24Z mordante $ */
/*
   Copyright (C) 2008 - 2011 by Jörg Hinrichs <joerg.hinrichs@alice-dsl.de>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#ifndef GUI_DIALOGS_EXPEDITE_HPP_INCLUDED
#define GUI_DIALOGS_EXPEDITE_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"

#include "config.hpp"
#include <set>

class game_display;
class team;
class unit;
class unit_map;
class hero_map;
class hero;
class artifical;
class unit_type;
class gamemap;

namespace gui2 {

class tlistbox;

class texpedite : public tdialog
{
public:
	explicit texpedite(game_display& gui, const gamemap& map, std::vector<team>& teams, unit_map& units, hero_map& heros, artifical& city, button_action* disband);

	int troop_index() const { return troop_index_; }

protected:
	/** Inherited from tdialog. */
	void pre_show(CVideo& video, twindow& window);

	/** Inherited from tdialog. */
	void post_show(twindow& window);

private:
	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	void disband(bool& handled, bool& halt,	twindow& window, int index);

	void type_selected(twindow& window);
	void refresh_tooltip(twindow& window);

	void merit(twindow& window);
	bool can_move(const unit& u);
private:
	game_display& gui_;
	const gamemap& map_;
	std::vector<team>& teams_;
	unit_map& units_;
	hero_map& heros_;

	artifical& city_;
	button_action* disband_;
	int troop_index_;

	tlistbox* hero_table_;
};

}

#endif

