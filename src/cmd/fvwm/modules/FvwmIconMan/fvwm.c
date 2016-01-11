/* This program is free software; you can redistribute it and/or modify
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
 */

#include "config.h"

#include "FvwmIconMan.h"
#include "x.h"
#include "xmanager.h"

#include <libs/fvwmlib.h>
#include <libs/FScreen.h>
#include <libs/Module.h>

static char const rcsid[] =
  "$Id: fvwm.c,v 1.34.2.3 2003/03/16 11:24:39 domivogt Exp $";

static WinData *fvwm_focus_win = NULL;

typedef struct {
  Ulong paging_enabled;
} m_toggle_paging_data;

typedef struct {
  Ulong desknum;
} m_new_desk_data;

typedef struct {
  Ulong x, y, desknum;
} m_new_page_data;

typedef struct {
  Ulong app_id, frame_id, dbase_entry;
} m_minimal_data;

typedef struct {
  Ulong app_id, frame_id, dbase_entry;
  Ulong xpos, ypos, icon_width, icon_height;
} m_icon_data;

typedef struct {
  Ulong app_id, frame_id, dbase_entry;
  union {
    Ulong name_long[1];
    Uchar name[4];
  } name;
} m_name_data;

#ifdef MINI_ICONS

typedef struct {
  Ulong app_id, frame_id, dbase_entry;
  Ulong width, height, depth, picture, mask;
  union {
    Ulong name_long[1];
    Uchar name[4];
  } name;
} m_mini_icon_data;

#endif

typedef union {
  m_toggle_paging_data toggle_paging_data;
  m_new_desk_data      new_desk_data;
  ConfigWinPacket      add_config_data;
  m_new_page_data      new_page_data;
  m_minimal_data       minimal_data;
  m_icon_data          icon_data;
  m_name_data          name_data;
#ifdef MINI_ICONS
  m_mini_icon_data     mini_icon_data;
#endif
} FvwmPacketBody;

/* only used by count_nonsticky_in_hashtab */
static WinManager *the_manager;

static int count_nonsticky_in_hashtab (void *arg)
{
  WinData *win = (WinData *)arg;
  WinManager *man = the_manager;

  if (!(IS_STICKY(win)) && win->complete && win->manager == man)
    return 1;
  return 0;
}

static void set_draw_mode (WinManager *man, int flag)
{
  int num;

  if (!man)
    return;

  if (man->we_are_drawing == 0 && flag) {
    draw_manager (man);
  }
  else if (man->we_are_drawing && !flag) {
    the_manager = man;
    num = accumulate_walk_hashtab (count_nonsticky_in_hashtab);
    ConsoleDebug (FVWM, "SetDrawMode on 0x%lx, num = %d\n",
		  (unsigned long)man, num);

    if (num == 0)
      return;
    man->configures_expected = num;
  }
  man->we_are_drawing = flag;
}

static int drawing (WinManager *man)
{
  if (!man)
    return 1;

  return man->we_are_drawing;
}

static void got_configure (WinManager *man)
{
  if (man && !man->we_are_drawing) {
    man->configures_expected--;
    ConsoleDebug (FVWM, "got_configure on 0x%lx, num_expected now = %d\n",
		  (unsigned long) man, man->configures_expected);
    if (man->configures_expected <= 0)
      set_draw_mode (man, 1);
  }
}

/* MMH <mikehan@best.com> 6/99
 * Following a gruesome hack this function has been hijacked.
 * The function is no longer about whether or not the window is in the
 * viewport, but whether or not the manager wants to display the window,
 * based on its current location.
 */
int win_in_viewport (WinData *win)
{
  WinManager *manager = win->manager;
  int flag = 0;
  int reverse = 0;
  rectangle g;

  assert (manager);

  if (IS_ICONIFIED(win) && !IS_ICON_SUPPRESSED(win))
  {
    g = win->icon_g;
  }
  else
  {
    g.x = win->x;
    g.y = win->y;
    g.width = win->width;
    g.height = win->height;
  }
  switch (manager->res) {
  case SHOW_GLOBAL:
    flag = 1;
    break;

  case NO_SHOW_DESKTOP:
    reverse = 1;
    /* fall through to next case */
  case SHOW_DESKTOP:
    if (IS_STICKY(win) || win->desknum == globals.desknum)
      flag = 1;
    break;

  case NO_SHOW_PAGE:
    reverse = 1;
    /* fall through to next case */
  case SHOW_PAGE:
    if (IS_STICKY(win)) {
      flag = 1;
    } else if (win->desknum == globals.desknum) {
      /* win and screen intersect if they are not disjoint in x and y */
      flag = RECTANGLES_INTERSECT(
	g.x, g.y, g.width, g.height,
	manager->managed_g.x, manager->managed_g.y,
	manager->managed_g.width, manager->managed_g.height);
    }
    break;

  case NO_SHOW_SCREEN:
    reverse = 1;
    /* fall through to next case */
  case SHOW_SCREEN:
    if (win->desknum == globals.desknum) {
      /* win and screen intersect if they are not disjoint in x and y */
      flag = RECTANGLES_INTERSECT(
	g.x, g.y, g.width, g.height,
	manager->managed_g.x, manager->managed_g.y,
	manager->managed_g.width, manager->managed_g.height);
    }
    break;
  }
  flag ^= reverse;

  return flag;
}


static WinData *id_to_win (Ulong id)
{
  WinData *win;
  win = find_win_hashtab (id);
  if (win == NULL) {
    win = new_windata ();
    win->app_id = id;
    win->app_id_set = 1;
    insert_win_hashtab (win);
  }
  return win;
}

static void set_win_configuration (WinData *win, FvwmPacketBody *body)
{
  win->desknum = body->add_config_data.desk;
  win->x = body->add_config_data.frame_x;
  win->y = body->add_config_data.frame_y;
  win->width = body->add_config_data.frame_width;
  win->height = body->add_config_data.frame_height;
  win->geometry_set = 1;
  memcpy(&(win->flags), &(body->add_config_data.flags), sizeof(win->flags));
}

static void handle_config_info (unsigned long *body)
{
  char *tline, *token;
  int color;

  tline = (char*)&(body[3]);
  token = PeekToken(tline, &tline);
  if (StrEquals(token, "Colorset"))
  {
    color = LoadColorset(tline);
    change_colorset(color);
  }
  else if (StrEquals(token, XINERAMA_CONFIG_STRING))
  {
    FScreenConfigureModule(tline);
  }
}

static void configure_window (FvwmPacketBody *body)
{
  Ulong app_id = body->add_config_data.w;
  WinData *win;
  ConsoleDebug (FVWM, "configure_window: %ld\n", app_id);

  win = id_to_win (app_id);

  set_win_configuration (win, body);

  check_win_complete (win);
  check_in_window (win);
  got_configure (win->manager);
}

static void focus_change (FvwmPacketBody *body)
{
  Ulong app_id = body->minimal_data.app_id;
  WinData *win = id_to_win (app_id);

  ConsoleDebug (FVWM, "Focus Change\n");
  ConsoleDebug (FVWM, "\tID: %ld\n", app_id);

  if (fvwm_focus_win)
  {
    del_win_state (fvwm_focus_win, FOCUS_CONTEXT);
    if (fvwm_focus_win->manager && fvwm_focus_win->manager->focus_button)
      fvwm_focus_win->manager->focus_button = NULL;
    fvwm_focus_win = NULL;
  }

  if (win->complete  &&
      win->button  &&
      win->manager &&
      win->manager->window_up  &&
      win->manager->followFocus) {
    win->manager->focus_button = win->button;
    add_win_state (win, FOCUS_CONTEXT);
  }

  globals.focus_win = win;
  fvwm_focus_win = win;
  ConsoleDebug (FVWM, "leaving focus_change\n");
}

static void res_name (FvwmPacketBody *body)
{
  Ulong app_id = body->name_data.app_id;
  Uchar *name = body->name_data.name.name;
  WinData *win;

  ConsoleDebug (FVWM, "In res_name\n");

  win = id_to_win (app_id);

  copy_string (&win->resname, (char *)name);
  change_windows_manager (win);

  ConsoleDebug (FVWM, "Exiting res_name\n");
}

static void class_name (FvwmPacketBody *body)
{
  Ulong app_id = body->name_data.app_id;
  Uchar *name = body->name_data.name.name;
  WinData *win;

  ConsoleDebug (FVWM, "In class_name\n");

  win = id_to_win (app_id);

  copy_string (&win->classname, (char *)name);
  change_windows_manager (win);

  ConsoleDebug (FVWM, "Exiting class_name\n");
}

static void icon_name (FvwmPacketBody *body)
{
  WinData *win;
  Ulong app_id;
  Uchar *name = body->name_data.name.name;

  ConsoleDebug (FVWM, "In icon_name\n");

  app_id = body->name_data.app_id;

  win = id_to_win (app_id);

  if (win->iconname && !strcmp (win->iconname, (char *)name)) {
    ConsoleDebug (FVWM, "No icon change: %s %s\n", win->iconname, name);
    return;
  }

  copy_string (&win->iconname, (char *)name);
  ConsoleDebug (FVWM, "new icon name: %s\n", win->iconname);
  if (change_windows_manager (win) == 0 && win->button &&
      (win->manager->format_depend & ICON_NAME)) {
    if (win->manager->sort) {
      resort_windows_button (win);
    }
  }

  ConsoleDebug (FVWM, "Exiting icon_name\n");
}

static void window_name (FvwmPacketBody *body)
{
  WinData *win;
  Ulong app_id;
  Uchar *name = body->name_data.name.name;

  ConsoleDebug (FVWM, "In window_name\n");

  app_id = body->name_data.app_id;

  win = id_to_win (app_id);

  /* This is necessary because bash seems to update the window title on
     every keystroke regardless of whether anything changes */
  if (win->titlename && !strcmp (win->titlename, (char *)name)) {
    ConsoleDebug (FVWM, "No name change: %s %s\n", win->titlename, name);
    return;
  }

  copy_string (&win->titlename, (char *)name);
  if (change_windows_manager (win) == 0 && win->button &&
      (win->manager->format_depend & TITLE_NAME)) {
    if (win->manager->sort) {
      resort_windows_button (win);
    }
  }
  ConsoleDebug (FVWM, "Exiting window_name\n");
}

static void new_window (FvwmPacketBody *body)
{
  WinData *win;

  win = id_to_win (body->add_config_data.w);
  memcpy(&(win->flags), &(body->add_config_data.flags), sizeof(win->flags));
  set_win_configuration (win, body);
  got_configure (win->manager);
  check_win_complete (win);
  check_in_window (win);
}

static void destroy_window (FvwmPacketBody *body)
{
  WinData *win;
  Ulong app_id;

  app_id = body->minimal_data.app_id;
  win = id_to_win (app_id);
  if (win == globals.focus_win)
    globals.focus_win = NULL;
  delete_win_hashtab (win);
  if (win->button) {
    ConsoleDebug (FVWM, "destroy_window: deleting windows_button\n");
    delete_windows_button (win);
  }
  if (win == fvwm_focus_win)
  {
    fvwm_focus_win = NULL;
  }
  free_windata (win);
}

#ifdef MINI_ICONS
static void mini_icon (FvwmPacketBody *body)
{
  Ulong app_id = body->mini_icon_data.app_id;
  WinData *win;

  win = id_to_win (app_id);
  set_win_picture (win, body->mini_icon_data.picture,
		   body->mini_icon_data.mask, body->mini_icon_data.depth,
		   body->mini_icon_data.width, body->mini_icon_data.height);


  ConsoleDebug (FVWM, "mini_icon: 0x%lx 0x%lx %dx%dx%d\n",
		(unsigned long) win->pic.picture,
		(unsigned long) win->pic.mask,
		win->pic.width, win->pic.height, win->pic.depth);
}
#endif

static void icon_location(FvwmPacketBody *body)
{
  Ulong app_id = body->minimal_data.app_id;
  WinData *win;

  win = id_to_win(app_id);
  if (win == NULL)
  {
    return;
  }
  win->icon_g.x = body->icon_data.xpos;
  win->icon_g.y = body->icon_data.ypos;
  win->icon_g.width = body->icon_data.icon_width;
  win->icon_g.height = body->icon_data.icon_height;
  check_in_window (win);
}

static void iconify(FvwmPacketBody *body, int dir)
{
  Ulong app_id = body->minimal_data.app_id;
  WinData *win;

  win = id_to_win(app_id);
  set_win_iconified(win, dir);
  icon_location(body);
  check_win_complete(win);
  check_in_window (win);
}

/* only used by new_desk */

static void update_win_in_hashtab (void *arg)
{
  WinData *p = (WinData *)arg;
  check_in_window (p);
}

static void new_desk (FvwmPacketBody *body)
{
  globals.desknum = body->new_desk_data.desknum;
  walk_hashtab (update_win_in_hashtab);

  draw_managers ();
}

static void sendtomodule (FvwmPacketBody *body)
{
  extern void execute_function (char *);
  Uchar *string = body->name_data.name.name;

  ConsoleDebug (FVWM, "Got string: %s\n", string);

  execute_function ((char *)string);
}

static void ProcessMessage (Ulong type, FvwmPacketBody *body)
{
  int i;

  ConsoleDebug (FVWM, "FVWM Message type: %ld\n", type);

  switch(type)
  {
  case M_CONFIG_INFO:
    ConsoleDebug (FVWM, "DEBUG::M_CONFIG_INFO\n");
    handle_config_info ((unsigned long*)body);
    break;

  case M_CONFIGURE_WINDOW:
    ConsoleDebug (FVWM, "DEBUG::M_CONFIGURE_WINDOW\n");
    configure_window (body);
    break;

  case M_FOCUS_CHANGE:
    ConsoleDebug (FVWM, "DEBUG::M_FOCUS_CHANGE\n");
    focus_change (body);
    break;

  case M_RES_NAME:
    ConsoleDebug (FVWM, "DEBUG::M_RES_NAME\n");
    res_name (body);
    break;

  case M_RES_CLASS:
    ConsoleDebug (FVWM, "DEBUG::M_RES_CLASS\n");
    class_name (body);
    break;

  case M_MAP:
    ConsoleDebug (FVWM, "DEBUG::M_MAP\n");
    break;

  case M_ADD_WINDOW:
    ConsoleDebug (FVWM, "DEBUG::M_ADD_WINDOW\n");
    new_window (body);
    break;

  case M_DESTROY_WINDOW:
    ConsoleDebug (FVWM, "DEBUG::M_DESTROY_WINDOW\n");
    destroy_window (body);
    break;

#ifdef MINI_ICONS
  case M_MINI_ICON:
    ConsoleDebug (FVWM, "DEBUG::M_MINI_ICON\n");
    mini_icon (body);
    break;
#endif

  case M_WINDOW_NAME:
    ConsoleDebug (FVWM, "DEBUG::M_WINDOW_NAME\n");
    window_name (body);
    break;

  case M_ICON_NAME:
    ConsoleDebug (FVWM, "DEBUG::M_ICON_NAME\n");
    icon_name (body);
    break;

  case M_DEICONIFY:
    ConsoleDebug (FVWM, "DEBUG::M_DEICONIFY\n");
    iconify (body, 0);
    SendUnlockNotification(Fvwm_fd);
    break;

  case M_ICONIFY:
    ConsoleDebug (FVWM, "DEBUG::M_ICONIFY\n");
    iconify (body, 1);
    SendUnlockNotification(Fvwm_fd);
    break;

  case M_ICON_LOCATION:
    icon_location(body);
    break;

  case M_END_WINDOWLIST:
    ConsoleDebug (FVWM, "DEBUG::M_END_WINDOWLIST\n");
    ConsoleDebug (FVWM,
		  "+++++ End window list +++++\n");
    if (globals.focus_win && globals.focus_win->button) {
	globals.focus_win->manager->focus_button = globals.focus_win->button;
    }
    globals.got_window_list = 1;
    for (i = 0; i < globals.num_managers; i++) {
      create_manager_window (i);
    }
    break;

  case M_NEW_DESK:
    ConsoleDebug (FVWM, "DEBUG::M_NEW_DESK\n");
    new_desk (body);
    break;

  case M_NEW_PAGE:
    ConsoleDebug (FVWM, "DEBUG::M_NEW_PAGE\n");
    if (globals.x == body->new_page_data.x &&
	globals.y == body->new_page_data.y &&
	globals.desknum == body->new_page_data.desknum) {
      ConsoleDebug (FVWM, "Useless NEW_PAGE received\n");
      break;
    }
    globals.x = body->new_page_data.x;
    globals.y = body->new_page_data.y;
    globals.desknum = body->new_page_data.desknum;
    if (fvwm_focus_win && fvwm_focus_win->manager &&
        fvwm_focus_win->manager->followFocus)
    {
      /* need to set the focus on a page change */
      add_win_state (fvwm_focus_win, FOCUS_CONTEXT);
    }
    for (i = 0; i < globals.num_managers; i++) {
      set_draw_mode (&globals.managers[i], 0);
    }
    break;

  case M_STRING:
    ConsoleDebug (FVWM, "DEBUG::M_STRING\n");
    sendtomodule (body);
    break;

  default:
    break;
  }

  check_managers_consistency();

  for (i = 0; i < globals.num_managers; i++) {
    if (drawing (&globals.managers[i]))
      draw_manager (&globals.managers[i]);
  }

  check_managers_consistency();
}

void ReadFvwmPipe (void)
{
  FvwmPacket* packet;

  PrintMemuse();
  ConsoleDebug(FVWM, "DEBUG: entering ReadFvwmPipe\n");

  if ( (packet = ReadFvwmPacket(Fvwm_fd[1])) == NULL )
  {
    exit(0);
  }
  else
  {
    ProcessMessage( packet->type, (FvwmPacketBody*) packet->body );
  }

  ConsoleDebug(FVWM, "DEBUG: leaving ReadFvwmPipe\n");
}