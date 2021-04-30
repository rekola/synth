#ifndef _FOURCC_H_
#define _FOURCC_H_

class FourCC {
 public:
  FourCC() {

  }
  bool operator== (const FourCC & other) {
    return value[0] == other.value[0] && value[1] == other.value[1] && value[2] == other.value[2] && value[3] == other.value[3];
  }

  bool operator== (const char * other) {
    return value[0] == other[0] && value[1] == other[1] && value[2] == other[2] && value[3] == other[3];
  }

  const char * data() const { return &(value[0]); }

 private:
  char value[4];
};

#endif
