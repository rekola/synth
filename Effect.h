#ifndef _EFFECT_H_
#define _EFFECT_H_

class SampleData;

class Effect {
 public:
  Effect() { }
  virtual ~Effect() { }
  
  virtual void apply(SampleData & input) = 0;
  
 private:
  
};

#endif
