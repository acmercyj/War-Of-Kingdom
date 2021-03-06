/* $Id: title_screen.hpp 48740 2011-03-05 10:01:34Z mordante $ */
/*
   Copyright (C) 2008 - 2011 by Mark de Wever <koraq@xs4all.nl>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#ifndef GUI_DIALOGS_STORY_SCREEN_HPP_INCLUDED
#define GUI_DIALOGS_STORY_SCREEN_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "config.hpp"

class display;

namespace gui2 {

void show_story(display &disp, const std::string &scenario_name, const config::const_child_itors& story);

/**
 * This class implements the title screen.
 *
 * The menu buttons return a result back to the caller with the button pressed.
 * So at the moment it only handles the tips itself.
 *
 * @todo Evaluate whether we can handle more buttons in this class.
 */
class tstory_screen : public tdialog
{
public:
	tstory_screen(const config& part);

	~tstory_screen();

	/**
	 * Values for the menu-items of the main menu.
	 *
	 * @todo Evaluate the best place for these items.
	 */
	enum legacy_result {NEXT, BACK, QUIT};

	legacy_result get_legacy_result() const { return legacy_result_; }
private:

	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	/** Inherited from tdialog. */
	virtual void post_build(CVideo& video, twindow& window);

	/** Inherited from tdialog. */
	void pre_show(CVideo& video, twindow& window);

	/** The progress bar time for the logo. */
	unsigned long logo_timer_id_;

	/**
	 * Updates the tip of day widget.
	 *
	 * @param window              The window being shown.
	 * @param previous            Show the previous tip, else shows the next
	 *                            one.
	 */
	void update_tip(twindow& window, const bool previous);

private:
	const config& part_;
	legacy_result legacy_result_;
};

} // namespace gui2

#endif
