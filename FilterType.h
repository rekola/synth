#ifndef _FILTERTYPE_H_
#define _FILTERTYPE_H_

enum class FilterType {
  lowpass = 0,
  highpass,
  bandpass,
  notch,
  peak,
  lowshelf,
  highshelf
};

#endif
