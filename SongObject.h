#ifndef _SONGOBJECT_H_
#define _SONGOBJECT_H_

#include <atomic>

class SongObject {
 public:
  SongObject() : id_(getNextId()) { }
  SongObject(int id) : id_(id != -1 ? id : getNextId()) { }
  virtual ~SongObject() { }
  
  int getId() const { return id_; }
  
  static int getNextId() {
    return next_id.fetch_add(1);
  }

 protected:
  void setId(int id) { id_ = id; }

 private:
  int id_;

  static std::atomic<int> next_id;
};

#endif
