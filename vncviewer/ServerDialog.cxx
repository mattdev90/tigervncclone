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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
#include <winsock2.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_ask.H>

#include <core/LogWriter.h>
#include <network/TcpSocket.h>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "ServerDialog.h"
#include "ProfileEditDialog.h"
#include "OptionsDialog.h"
#include "CConn.h"
#include "i18n.h"
#include "vncviewer.h"
#include "parameters.h"

static core::LogWriter vlog("ServerDialog");

ServerDialog::ServerDialog()
  : Fl_Window(480, 0, "TigerVNC")
{
  int x = OUTER_MARGIN;
  int y = OUTER_MARGIN;

  // "VNC Hosts" label
  Fl_Box* label = new Fl_Box(x, y, w() - OUTER_MARGIN*2, FL_NORMAL_SIZE + 4,
                              _("VNC Hosts"));
  label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  label->labelfont(FL_HELVETICA_BOLD);
  y += label->h() + TIGHT_MARGIN;

  // Profile list
  int listH = 200;
  profileList = new Fl_Hold_Browser(x, y, w() - OUTER_MARGIN*2, listH);
  profileList->column_widths(new int[3]{50, 220, 0});
  profileList->column_char('\t');
  profileList->callback(handleProfileBrowser, this);
  profileList->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
  y += listH + INNER_MARGIN;

  // Button bar
  int x2 = x;

  Fl_Button* addBtn = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT,
                                    _("Add Host"));
  addBtn->callback(handleAddHost, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  editButton = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Edit"));
  editButton->callback(handleEdit, this);
  editButton->deactivate();
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  deleteButton = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Delete"));
  deleteButton->callback(handleDelete, this);
  deleteButton->deactivate();

  // Options on the right
  Fl_Button* optBtn = new Fl_Button(w() - OUTER_MARGIN - BUTTON_WIDTH, y,
                                    BUTTON_WIDTH, BUTTON_HEIGHT, _("Options..."));
  optBtn->callback(handleOptions, this);

  y += BUTTON_HEIGHT + INNER_MARGIN;

  // Divider
  Fl_Box* divider = new Fl_Box(0, y, w(), 2);
  divider->box(FL_THIN_DOWN_FRAME);
  y += divider->h() + INNER_MARGIN;

  // Bottom row
  y += OUTER_MARGIN - INNER_MARGIN;

  Fl_Button* aboutBtn = new Fl_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT,
                                      _("About..."));
  aboutBtn->callback(handleAbout, this);

  Fl_Button* cancelBtn = new Fl_Button(w() - OUTER_MARGIN - BUTTON_WIDTH, y,
                                       BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  cancelBtn->callback(handleCancel, this);

  y += BUTTON_HEIGHT + INNER_MARGIN;

  resizable(nullptr);
  h(y - INNER_MARGIN + OUTER_MARGIN);

  callback(handleCancel, this);
}

ServerDialog::~ServerDialog() {}

void ServerDialog::run(const char* /*servername*/, char *newservername)
{
  ServerDialog dialog;

  dialog.show();

  try {
    dialog.loadProfiles();
    dialog.populateProfileList();
  } catch (std::exception& e) {
    vlog.error(_("Unable to load profiles: %s"), e.what());
  }

  while (dialog.shown()) Fl::wait();

  if (dialog.resultServerName.empty()) {
    newservername[0] = '\0';
    return;
  }

  strncpy(newservername, dialog.resultServerName.c_str(), VNCSERVERNAMELEN);
  newservername[VNCSERVERNAMELEN - 1] = '\0';
}

void ServerDialog::loadProfiles()
{
  profiles = loadAllProfiles();
}

void ServerDialog::populateProfileList()
{
  profileList->clear();
  for (const ProfileInfo& p : profiles) {
    std::string displayName = p.profileName.empty() ?
                              p.serverName : p.profileName;
    std::string indicator = p.hasPassword ? "[*]" : "[ ]";
    std::string entry = indicator + "\t" + displayName + "\t" + p.serverName;
    profileList->add(entry.c_str());
  }
  updateButtonStates();
}

void ServerDialog::updateButtonStates()
{
  bool hasSelection = profileList->value() > 0;
  if (hasSelection) {
    editButton->activate();
    deleteButton->activate();
  } else {
    editButton->deactivate();
    deleteButton->deactivate();
  }
}

void ServerDialog::connectToProfile(int index)
{
  if (index < 0 || index >= (int)profiles.size())
    return;

  const ProfileInfo& profile = profiles[index];

  try {
    loadViewerParameters(profile.filePath.c_str());
  } catch (std::exception& e) {
    vlog.error(_("Failed to load profile settings: %s"), e.what());
  }

  CConn::activeProfilePath = profile.filePath;
  resultServerName = profile.serverName;
  hide();
}

void ServerDialog::openEditDialog(int index)
{
  bool isAdd = (index < 0);

  const char* currentServer = isAdd ? "" : profiles[index].serverName.c_str();
  const char* currentName   = isAdd ? "" : profiles[index].profileName.c_str();

  std::string currentPwd;
  if (!isAdd)
    currentPwd = loadPasswordFromProfile(profiles[index].filePath.c_str());

  ProfileEditDialog dlg(
    isAdd ? _("Add VNC Host") : _("Edit VNC Host"),
    currentServer, currentName, currentPwd.c_str());
  dlg.show();
  while (dlg.shown()) Fl::wait();

  if (!dlg.wasSaved())
    return;

  std::string server = dlg.getServerName();
  if (server.empty()) {
    fl_alert(_("Host address cannot be empty."));
    return;
  }

  std::string name = dlg.getProfileName();
  std::string pwd  = dlg.getPassword();

  try {
    if (!isAdd) {
      // Delete old profile file (server address may have changed → new filename)
      deleteProfile(profiles[index].filePath.c_str());
    }

    saveProfile(server.c_str(),
                name.empty() ? nullptr : name.c_str(),
                pwd.empty()  ? nullptr : pwd.c_str());

    loadProfiles();
    populateProfileList();
  } catch (std::exception& e) {
    vlog.error(_("Failed to save profile: %s"), e.what());
    fl_alert(_("Failed to save profile:\n\n%s"), e.what());
  }
}

void ServerDialog::handleProfileBrowser(Fl_Widget* /*w*/, void* data)
{
  ServerDialog* dialog = (ServerDialog*)data;
  int selected = dialog->profileList->value();

  dialog->updateButtonStates();

  if (selected <= 0)
    return;

  int idx = selected - 1;

  // Right-click: show popup menu
  if (Fl::event_button() == FL_RIGHT_MOUSE) {
    Fl_Menu_Button popup(Fl::event_x(), Fl::event_y(), 1, 1);
    popup.add(_("Connect"),    0, nullptr, nullptr, 0);
    popup.add(_("Edit"),       0, nullptr, nullptr, 0);
    popup.add(_("Delete"),     0, nullptr, nullptr, FL_MENU_DIVIDER);
    popup.popup();

    const Fl_Menu_Item* chosen = popup.mvalue();
    if (!chosen) return;

    std::string label = chosen->label() ? chosen->label() : "";
    if (label == std::string(_("Connect"))) {
      dialog->connectToProfile(idx);
    } else if (label == std::string(_("Edit"))) {
      dialog->openEditDialog(idx);
    } else if (label == std::string(_("Delete"))) {
      handleDelete(nullptr, data);
    }
    return;
  }

  // Double left-click: connect
  if (Fl::event_clicks() > 0 && Fl::event_button() == FL_LEFT_MOUSE) {
    dialog->connectToProfile(idx);
    return;
  }
}

void ServerDialog::handleAddHost(Fl_Widget* /*w*/, void* data)
{
  ServerDialog* dialog = (ServerDialog*)data;
  dialog->openEditDialog(-1);
}

void ServerDialog::handleEdit(Fl_Widget* /*w*/, void* data)
{
  ServerDialog* dialog = (ServerDialog*)data;
  int selected = dialog->profileList->value();
  if (selected <= 0) return;
  dialog->openEditDialog(selected - 1);
}

void ServerDialog::handleDelete(Fl_Widget* /*w*/, void* data)
{
  ServerDialog* dialog = (ServerDialog*)data;
  int selected = dialog->profileList->value();

  if (selected <= 0 || selected - 1 >= (int)dialog->profiles.size())
    return;

  const ProfileInfo& profile = dialog->profiles[selected - 1];
  std::string displayName = profile.profileName.empty() ?
                            profile.serverName : profile.profileName;

  int choice = fl_choice(_("Delete \"%s\"?"),
                         _("Cancel"), _("Delete"), nullptr,
                         displayName.c_str());
  if (choice != 1) return;

  try {
    deleteProfile(profile.filePath.c_str());
    dialog->profiles.erase(dialog->profiles.begin() + (selected - 1));
    dialog->populateProfileList();
  } catch (std::exception& e) {
    vlog.error(_("Failed to delete profile: %s"), e.what());
    fl_alert(_("Failed to delete:\n\n%s"), e.what());
  }
}

void ServerDialog::handleOptions(Fl_Widget* /*w*/, void* /*data*/)
{
  OptionsDialog::showDialog();
}

void ServerDialog::handleAbout(Fl_Widget* /*w*/, void* /*data*/)
{
  about_vncviewer();
}

void ServerDialog::handleCancel(Fl_Widget* /*w*/, void* data)
{
  ServerDialog* dialog = (ServerDialog*)data;
  dialog->resultServerName = "";
  dialog->hide();
}
