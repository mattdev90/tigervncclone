#ifndef __PROFILEEDITDIALOG_H__
#define __PROFILEEDITDIALOG_H__

#include <string>
#include <FL/Fl_Window.H>

class Fl_Input;
class Fl_Secret_Input;
class Fl_Check_Button;

class ProfileEditDialog : public Fl_Window {
public:
  ProfileEditDialog(const char* title, const char* serverName,
                    const char* profileName, const char* password);
  ~ProfileEditDialog();

  std::string getServerName();
  std::string getProfileName();
  std::string getPassword();
  bool wasSaved();

private:
  static void handleSave(Fl_Widget* w, void* data);
  static void handleCancel(Fl_Widget* w, void* data);

  Fl_Input* serverInput;
  Fl_Input* nameInput;
  Fl_Secret_Input* passwdInput;
  Fl_Check_Button* savePasswdCheck;

  bool saved_;
};

#endif
