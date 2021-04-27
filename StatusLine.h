#ifndef _STATUSLINE_H_
#define _STATUSLINE_H_

#include "UIElement.h"
#include "UIInput.h"

class StatusLine : public UIElement {
 public:
  StatusLine(UIPlane & parent) : UIElement(parent) { }

  void setMessage(const std::string & s) {
    if (getPlane().readerActive()) {
      pending_message = s;
    } else {
      erase();
      putstr(0, 0, s.c_str());
    }
  }

  bool offerInput(const UIInput & input) override {
    if (getPlane().readerActive()) {
      if (input.getId() == NCKEY_ENTER) {
	std::string cmd = closeReader();
	if (!getController().sendCommand(cmd)) {
	  setMessage("Invalid command");
	}
	return true;
      } else if (input.hasCtrl() && input.getId() == 'g') {
	closeReader();
	return true;
      } else {
	return UIElement::offerInput(input);	
      }
    } else if (input.getId() == NCKEY_ESC) {
      meta_pressed = true;
    } else if (meta_pressed) {
      if (input.getId() == 'x' || input.getId() == 'X') {
    	getPlane().showReader();
	setMessage("M-x ");
	return true;
      }
      meta_pressed = false;
    }
    return false;
  }

  std::string closeReader() {
    std::string cmd = getPlane().closeReader();
    if (pending_message.empty()) {
      setMessage("");
    } else {
      setMessage(pending_message);
      pending_message.clear();
    }
    return cmd;
  }
  
private:
  bool meta_pressed = false;
  std::string pending_message;
};

#endif
