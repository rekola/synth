#ifndef _UICOLOR_H_
#define _UICOLOR_H_

static inline int color_get_xdigit(char c) {                                                             
  if (c >= '0' && c <= '9') {                                                               
    return c - '0';                                                                         
  } else if (c >= 'A' && c <= 'F') {                                                        
    return c - 'A' + 10;                                                                    
  } else if (c >= 'a' && c <= 'f') {                                                        
    return c - 'a' + 10;                                                                    
  } else {                                                                                  
    return 0;                                                                               
  }                                                                                         
}

class UIColor {
 public:
  explicit UIColor() : red(0), green(0), blue(0) { }
  UIColor(std::string s) {
    setValue(s);
  }
  UIColor(const char * s) {
    setValue(s);
  }
  UIColor & operator=(std::string s) {
    setValue(s);
    return *this;
  }
  UIColor & operator=(const char * s) {
    setValue(s);
    return *this;
  }

  int getRed() const { return red; }
  int getGreen() const { return green; }
  int getBlue() const { return blue; }

 private:
  void setValue(std::string s) {
    size_t pos = 0;
    if (s.size() && s[0] == '#') pos++;
    if (s.size() >= pos + 6) {
      red = color_get_xdigit(s[pos]) * 16 + color_get_xdigit(s[pos + 1]);
      green = color_get_xdigit(s[pos + 2]) * 16 + color_get_xdigit(s[pos + 3]);
      blue = color_get_xdigit(s[pos + 4]) * 16 + color_get_xdigit(s[pos+5]);
    } else if (s.size() >= pos + 3) {
      int r = color_get_xdigit(s[pos]);
      int g = color_get_xdigit(s[pos + 1]);
      int b = color_get_xdigit(s[pos + 2]);
      red = (r * 16 + r) / 255.0f;
      green = (g * 16 + g) / 255.0f;
      blue = (b * 16 + b) / 255.0f;
    } else {
      red = green = blue = 0;
    }
  }
  
  unsigned char red, green, blue;
};

#endif
