/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2026 TigerVNC Contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifndef __SERVERDIALOG_H__
#define __SERVERDIALOG_H__

#include <FL/Fl_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <string>
#include <vector>

#include "parameters.h"

class Fl_Widget;
class Fl_Button;

class ServerDialog : public Fl_Window {
protected:
  ServerDialog();
  ~ServerDialog();

public:
  static void run(const char* servername, char *newservername);

protected:
  static void handleAddHost(Fl_Widget *widget, void *data);
  static void handleEdit(Fl_Widget *widget, void *data);
  static void handleDelete(Fl_Widget *widget, void *data);
  static void handleOptions(Fl_Widget *widget, void *data);
  static void handleAbout(Fl_Widget *widget, void *data);
  static void handleCancel(Fl_Widget *widget, void *data);
  static void handleProfileBrowser(Fl_Widget *widget, void *data);

private:
  void loadProfiles();
  void populateProfileList();
  void connectToProfile(int index);
  void openEditDialog(int index);    // -1 for add mode
  void updateButtonStates();

protected:
  Fl_Hold_Browser *profileList;
  Fl_Button *editButton;
  Fl_Button *deleteButton;
  std::vector<ProfileInfo> profiles;
  std::string resultServerName;
};

#endif
