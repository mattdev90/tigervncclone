/* Copyright 2026 TigerVNC Contributors
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
#include <FL/Fl_Input.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Box.H>

#include "fltk/layout.h"
#include "ProfileEditDialog.h"
#include "i18n.h"

ProfileEditDialog::ProfileEditDialog(const char* title,
                                     const char* serverName,
                                     const char* profileName,
                                     const char* password)
  : Fl_Window(380, 0, title), saved_(false)
{
  int x = OUTER_MARGIN;
  int y = OUTER_MARGIN;
  int inputW = w() - OUTER_MARGIN * 2;

  // Host field
  y += INPUT_LABEL_OFFSET;
  serverInput = new Fl_Input(
    LBLLEFT(x, y, inputW, INPUT_HEIGHT, _("Host:")));
  serverInput->value(serverName);
  y += INPUT_HEIGHT + INNER_MARGIN;

  // Name field
  y += INPUT_LABEL_OFFSET;
  nameInput = new Fl_Input(
    LBLLEFT(x, y, inputW, INPUT_HEIGHT, _("Name:")));
  nameInput->value(profileName);
  y += INPUT_HEIGHT + INNER_MARGIN;

  // Password field
  y += INPUT_LABEL_OFFSET;
  passwdInput = new Fl_Secret_Input(
    LBLLEFT(x, y, inputW, INPUT_HEIGHT, _("Password:")));
  if (password && password[0])
    passwdInput->value(password);
  y += INPUT_HEIGHT + INNER_MARGIN;

  // Save password checkbox
  savePasswdCheck = new Fl_Check_Button(
    LBLRIGHT(x, y, CHECK_MIN_WIDTH, CHECK_HEIGHT, _("Save password")));
  if (password && password[0])
    savePasswdCheck->value(1);
  y += CHECK_HEIGHT + INNER_MARGIN;

  // Buttons
  y += OUTER_MARGIN - INNER_MARGIN;
  int x2 = w() - OUTER_MARGIN;

  x2 -= BUTTON_WIDTH;
  Fl_Return_Button* saveBtn = new Fl_Return_Button(
    x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Save"));
  saveBtn->callback(handleSave, this);
  x2 -= INNER_MARGIN;

  x2 -= BUTTON_WIDTH;
  Fl_Button* cancelBtn = new Fl_Button(
    x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  cancelBtn->callback(handleCancel, this);
  cancelBtn->shortcut(FL_Escape);

  y += BUTTON_HEIGHT + OUTER_MARGIN;

  end();
  size(w(), y);
  set_modal();
}

ProfileEditDialog::~ProfileEditDialog() {}

std::string ProfileEditDialog::getServerName()
{
  return serverInput->value() ? serverInput->value() : "";
}

std::string ProfileEditDialog::getProfileName()
{
  return nameInput->value() ? nameInput->value() : "";
}

std::string ProfileEditDialog::getPassword()
{
  if (savePasswdCheck->value() && passwdInput->value() && passwdInput->value()[0])
    return passwdInput->value();
  return "";
}

bool ProfileEditDialog::wasSaved()
{
  return saved_;
}

void ProfileEditDialog::handleSave(Fl_Widget* /*w*/, void* data)
{
  ProfileEditDialog* self = (ProfileEditDialog*)data;
  self->saved_ = true;
  self->hide();
}

void ProfileEditDialog::handleCancel(Fl_Widget* /*w*/, void* data)
{
  ProfileEditDialog* self = (ProfileEditDialog*)data;
  self->hide();
}
