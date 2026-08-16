#ifndef _EVENT_H_
#define _EVENT_H_

class EventHandler;

class Event {
 public:
  explicit Event() { }
  virtual ~Event() { }

  virtual void dispatch(EventHandler & evh) = 0;

  void redraw() { need_redraw = true; }
  bool needRedraw() const { return need_redraw; }
  
private:
  bool need_redraw = false;
};

#endif
