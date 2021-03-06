
/*
 * CINELERRA
 * Copyright (C) 2008 Adam Williams <broadcast at earthling dot net>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

#include "language.h"
#include "mainmenu.h"
#include "mainerror.h"
#include "mwindow.h"
#include "mwindowgui.h"
#include "playback3d.h"
#include "quit.h"
#include "question.h"
#include "record.h"
#include "render.h"
#include "savefile.h"
#include "mainsession.h"


Quit::Quit(MWindow *mwindow)
 : BC_MenuItem(_("Quit"), "q", 'q'), Thread() 
{ 
	this->mwindow = mwindow; 
}

void Quit::create_objects(Save *save)
{ 
	this->save = save; 
}

int Quit::handle_event() 
{
	if(mwindow->session->changes_made ||
		mwindow->gui->mainmenu->record->current_state ||
		mwindow->render->in_progress) 
	{
		start();
	}
	else 
	{        // quit
		mwindow->gui->unlock_window();
		mwindow->interrupt_indexes();
		mwindow->playback_3d->quit();
		mwindow->gui->lock_window();
		return 1;
	}
	return 0;
}

void Quit::run()
{
	int result = 0;
// Test execution conditions
	if(mwindow->gui->mainmenu->record->current_state == RECORD_CAPTURING)
	{
		errorbox(_("Can't quit while capturing is in progress."));
		return;
	}
	else
	if(mwindow->render->running())
	{
		errorbox(_("Can't quit while a render is in progress."));
		return;
	}
// Quit
	{
		QuestionWindow confirm(mwindow);
		confirm.create_objects(_("Save edit list before exiting?\n( Answering \"No\" will destroy changes )"), 1);
		result = confirm.run_window();
	}

	switch(result)
	{
	case 0:          // quit
		if(mwindow->gui)
		{
			mwindow->interrupt_indexes();
			mwindow->playback_3d->quit();
		}
		break;

	case 1:        // cancel
		break;

	case 2:           // save
		save->save_before_quit();
			break;
	}
}
