
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

#include "cinelerra.h"
#include "clip.h"
#include "cplayback.h"
#include "cwindow.h"
#include "bchash.h"
#include "edl.h"
#include "edlsession.h"
#include "filexml.h"
#include "interlacemodes.h"
#include "language.h"
#include "levelwindow.h"
#include "mainerror.h"
#include "mainundo.h"
#include "mainmenu.h"
#include "mutex.h"
#include "mwindow.h"
#include "mwindowgui.h"
#include "new.h"
#include "newpresets.h"
#include "mainsession.h"
#include "patchbay.h"
#include "preferences.h"
#include "selection.h"
#include "theme.h"
#include "transportque.h"
#include "vplayback.h"
#include "vwindow.h"


#include <string.h>


#define WIDTH 610
#define HEIGHT 430


New::New(MWindow *mwindow)
 : BC_MenuItem(_("New..."), "n", 'n')
{
	this->mwindow = mwindow;
	script = 0;
}

int New::create_objects()
{
	thread = new NewThread(mwindow, this);
	return 0;
}

int New::handle_event() 
{
	if(thread->running())
	{
		thread->window_lock->lock("New::handle_event");
		if(thread->nwindow)
		{
			thread->nwindow->lock_window("New::handle_event");
			thread->nwindow->raise_window();
			thread->nwindow->unlock_window();
		}
		thread->window_lock->unlock();
		return 1;
	}
	mwindow->edl->save_defaults(mwindow->defaults);
	create_new_edl();
	thread->start(); 

	return 1;
}

void New::create_new_edl()
{
	new_edl = new EDL;
	new_edl->create_objects();
	new_edl->load_defaults(mwindow->defaults);
}


int New::create_new_project()
{
	mwindow->cwindow->playback_engine->que->send_command(STOP,
		CHANGE_NONE, 
		0,
		0);
	mwindow->vwindow->playback_engine->que->send_command(STOP,
		CHANGE_NONE, 
		0,
		0);
	mwindow->cwindow->playback_engine->interrupt_playback(0);
	mwindow->vwindow->playback_engine->interrupt_playback(0);

	mwindow->gui->lock_window();
	mwindow->reset_caches();

	memcpy(new_edl->session->achannel_positions,
		&mwindow->preferences->channel_positions[
			MAXCHANNELS * (new_edl->session->audio_channels - 1)],
		sizeof(int) * MAXCHANNELS);
	new_edl->session->boundaries();
	new_edl->create_default_tracks();

	mwindow->set_filename("");
	mwindow->undo->update_undo(_("New"), LOAD_ALL);

	mwindow->hide_plugins();

	if(SampleRateSelection::limits(&new_edl->session->sample_rate) < 0)
		errorbox(_("Sample rate is out of limits (%d..%d).\nCorrection applied."),
			MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
	if(FrameRateSelection::limits(&new_edl->session->frame_rate) < 0)
		errorbox(_("Frame rate is out of limits (%d..%d).\nCorrection applied."),
			MIN_FRAME_RATE, MAX_FRAME_RATE);
	if(FrameSizeSelection::limits(&new_edl->session->output_w,
			&new_edl->session->output_h) < 0)
		errorbox(_("Frame size is out of limits (%d..%dx%d..%d).\nCorrection applied."),
			MIN_FRAME_WIDTH, MAX_FRAME_WIDTH, MIN_FRAME_HEIGHT, MAX_FRAME_WIDTH);

	delete mwindow->edl;
	mwindow->edl = new_edl;
	mwindow->save_defaults();

// Load file sequence
	mwindow->update_project(LOAD_REPLACE);
	mwindow->session->changes_made = 0;
	mwindow->gui->unlock_window();
	return 0;
}

NewThread::NewThread(MWindow *mwindow, New *new_project)
 : Thread()
{
	this->mwindow = mwindow;
	this->new_project = new_project;
	window_lock = new Mutex("NewThread::window_lock");
}

NewThread::~NewThread()
{
	delete window_lock;
}


void NewThread::run()
{
	int result = 0;

	int x = mwindow->gui->get_root_w(0, 1) / 2 - WIDTH / 2;
	int y = mwindow->gui->get_root_h(1) / 2 - HEIGHT / 2;

	window_lock->lock("NewThread::run 1\n");
	nwindow = new NewWindow(mwindow, this, x, y);
	nwindow->create_objects();
	window_lock->unlock();

	result = nwindow->run_window();

	window_lock->lock("NewThread::run 2\n");
	delete nwindow;	
	nwindow = 0;
	window_lock->unlock();

	new_project->new_edl->save_defaults(mwindow->defaults);
	mwindow->defaults->save();

	if(result)
	{
// Aborted
		delete new_project->new_edl;
	}
	else
	{
		new_project->create_new_project();
	}
}

#if 0
N_("Cinelerra: New Project");
#endif

NewWindow::NewWindow(MWindow *mwindow, NewThread *new_thread, int x, int y)
 : BC_Window(MWindow::create_title(N_("New Project")),
 		x,
		y,
		WIDTH, 
		HEIGHT,
		-1,
		-1,
		0,
		0,
		1)
{
	this->mwindow = mwindow;
	this->new_thread = new_thread;
	this->new_edl = new_thread->new_project->new_edl;
	format_presets = 0;
}

NewWindow::~NewWindow()
{
	delete format_presets;
	delete cmodel_selection;
}

int NewWindow::create_objects()
{
	int x = 10, y = 10, x1, y1;
	BC_TextBox *textbox;

	mwindow->theme->draw_new_bg(this);

	add_subwindow(new BC_Title(x, y, _("Parameters for the new project:")));
	y += 20;

	format_presets = new NewPresets(this, x, y);
	format_presets->set_edl(new_edl);

	y += 60;
	y1 = y;
	add_subwindow(new BC_Title(x, y, _("Audio"), LARGEFONT));
	y += 30;

	x1 = x;
	add_subwindow(new BC_Title(x1, y, _("Tracks:")));
	x1 += 100;
	add_subwindow(atracks = new NewATracks(this, "", x1, y));
	x1 += atracks->get_w();
	add_subwindow(new NewATracksTumbler(this, x1, y));
	y += atracks->get_h() + 5;

	x1 = x;
	add_subwindow(new BC_Title(x1, y, _("Channels:")));
	x1 += 100;
	add_subwindow(achannels = new NewAChannels(this, "", x1, y));
	x1 += achannels->get_w();
	add_subwindow(new NewAChannelsTumbler(this, x1, y));
	y += achannels->get_h() + 5;

	x1 = x;
	add_subwindow(new BC_Title(x1, y, _("Samplerate:")));
	x1 += 100;
	add_subwindow(sample_rate = new SampleRateSelection(x1, y, this,
			&new_edl->session->sample_rate));
	x += 250;
	y = y1;
	add_subwindow(new BC_Title(x, y, _("Video"), LARGEFONT));
	y += 30;
	x1 = x;
	add_subwindow(new BC_Title(x1, y, _("Tracks:")));
	x1 += 100;
	add_subwindow(vtracks = new NewVTracks(this, "", x1, y));
	x1 += vtracks->get_w();
	add_subwindow(new NewVTracksTumbler(this, x1, y));
	y += vtracks->get_h() + 5;

// 	x1 = x;
// 	add_subwindow(new BC_Title(x1, y, _("Channels:")));
// 	x1 += 100;
// 	add_subwindow(vchannels = new NewVChannels(this, "", x1, y));
// 	x1 += vchannels->get_w();
// 	add_subwindow(new NewVChannelsTumbler(this, x1, y));
// 	y += vchannels->get_h() + 5;
	x1 = x;
	add_subwindow(new BC_Title(x1, y, _("Framerate:")));
	x1 += 100;
	add_subwindow(frame_rate = new FrameRateSelection(x1, y, this,
		&new_edl->session->frame_rate));
	y += frame_rate->get_h() + 5;

//	x1 = x;
//	add_subwindow(new BC_Title(x1, y, _("Canvas size:")));
// 	x1 += 100;
// 	add_subwindow(canvas_w_text = new NewTrackW(this, x1, y));
// 	x1 += canvas_w_text->get_w() + 2;
// 	add_subwindow(new BC_Title(x1, y, "x"));
// 	x1 += 10;
// 	add_subwindow(canvas_h_text = new NewTrackH(this, x1, y));
// 	x1 += canvas_h_text->get_w();
//	x1 += 100;
//	add_subwindow(new NewCloneToggle(mwindow, this, x1, y));
//	y += canvas_h_text->get_h() + 5;

	x1 = x;
	add_subwindow(new BC_Title(x1, y, _("Canvas size:")));
	x1 += 100;
	add_subwindow(framesize_selection = new FrameSizeSelection(x1, y,
		x1 + SELECTION_TB_WIDTH + 10, y,
		this, &new_edl->session->output_w, &new_edl->session->output_h));
	y += framesize_selection->get_h() + 5;

	x1 = x;
	add_subwindow(new BC_Title(x1, y, _("Aspect ratio:")));
	x1 += 100;

	add_subwindow(aspectratio_selection = new AspectRatioSelection(x1, y,
		x1 + SELECTION_TB_WIDTH + 10, y,
		this, &new_edl->session->aspect_w, &new_edl->session->aspect_h,
		&new_edl->session->output_w, &new_edl->session->output_h));

	y += aspectratio_selection->get_h() + 5;

	add_subwindow(new BC_Title(x, y, _("Color model:")));
	cmodel_selection = new ColormodelSelection(x + 100, y, this,
		&new_edl->session->color_model);
	y += cmodel_selection->selection->get_h() + 5;

	// --------------------
	add_subwindow(new BC_Title(x, y, _("Interlace mode:")));
	add_subwindow(interlace_selection = new InterlaceModeSelection(x + 100, y, this,
		&new_edl->session->interlace_mode));
	y += interlace_selection->get_h() + 5;

	add_subwindow(new BC_OKButton(this, 
		mwindow->theme->get_image_set("new_ok_images")));
	add_subwindow(new BC_CancelButton(this, 
		mwindow->theme->get_image_set("new_cancel_images")));
	flash();
	update();
	show_window();
	return 0;
}

int NewWindow::update()
{
	char string[BCTEXTLEN];
	atracks->update((int64_t)new_edl->session->audio_tracks);
	achannels->update((int64_t)new_edl->session->audio_channels);
	sample_rate->update(new_edl->session->sample_rate);
	vtracks->update((int64_t)new_edl->session->video_tracks);
	frame_rate->update((float)new_edl->session->frame_rate);
	framesize_selection->update(new_edl->session->output_w,
		new_edl->session->output_h);
	aspectratio_selection->update_auto(new_edl->session->aspect_w,
		new_edl->session->aspect_h);
	interlace_selection->update(new_edl->session->interlace_mode);
	cmodel_selection->update(new_edl->session->color_model);
	return 0;
}







NewPresets::NewPresets(NewWindow *gui, int x, int y)
 : FormatPresets(gui, x, y)
{
	new_gui = gui;
}

int NewPresets::handle_event()
{
	new_gui->update();
	return 1;
}


NewATracks::NewATracks(NewWindow *nwindow, const char *text, int x, int y)
 : BC_TextBox(x, y, 90, 1, text)
{
	this->nwindow = nwindow;
}

int NewATracks::handle_event()
{
	nwindow->new_edl->session->audio_tracks = atol(get_text());
	return 1;
}

NewATracksTumbler::NewATracksTumbler(NewWindow *nwindow, int x, int y)
 : BC_Tumbler(x, y)
{
	this->nwindow = nwindow;
}
int NewATracksTumbler::handle_up_event()
{
	nwindow->new_edl->session->audio_tracks++;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}
int NewATracksTumbler::handle_down_event()
{
	nwindow->new_edl->session->audio_tracks--;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}

NewAChannels::NewAChannels(NewWindow *nwindow, const char *text, int x, int y)
 : BC_TextBox(x, y, 90, 1, text)
{
	this->nwindow = nwindow;
} 

int NewAChannels::handle_event()
{
	nwindow->new_edl->session->audio_channels = atol(get_text());
	return 1;
}

NewAChannelsTumbler::NewAChannelsTumbler(NewWindow *nwindow, int x, int y)
 : BC_Tumbler(x, y)
{
	this->nwindow = nwindow;
}
int NewAChannelsTumbler::handle_up_event()
{
	nwindow->new_edl->session->audio_channels++;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}
int NewAChannelsTumbler::handle_down_event()
{
	nwindow->new_edl->session->audio_channels--;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}


NewVTracks::NewVTracks(NewWindow *nwindow, const char *text, int x, int y)
 : BC_TextBox(x, y, 90, 1, text)
{
	this->nwindow = nwindow;
}

int NewVTracks::handle_event()
{
	nwindow->new_edl->session->video_tracks = atol(get_text());
	return 1;
}

NewVTracksTumbler::NewVTracksTumbler(NewWindow *nwindow, int x, int y)
 : BC_Tumbler(x, y)
{
	this->nwindow = nwindow;
}
int NewVTracksTumbler::handle_up_event()
{
	nwindow->new_edl->session->video_tracks++;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}
int NewVTracksTumbler::handle_down_event()
{
	nwindow->new_edl->session->video_tracks--;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}

NewVChannels::NewVChannels(NewWindow *nwindow, const char *text, int x, int y)
 : BC_TextBox(x, y, 90, 1, text)
{
	this->nwindow = nwindow;
}

int NewVChannels::handle_event()
{
	nwindow->new_edl->session->video_channels = atol(get_text());
	return 1;
}

NewVChannelsTumbler::NewVChannelsTumbler(NewWindow *nwindow, int x, int y)
 : BC_Tumbler(x, y)
{
	this->nwindow = nwindow;
}
int NewVChannelsTumbler::handle_up_event()
{
	nwindow->new_edl->session->video_channels++;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}
int NewVChannelsTumbler::handle_down_event()
{
	nwindow->new_edl->session->video_channels--;
	nwindow->new_edl->boundaries();
	nwindow->update();
	return 1;
}
