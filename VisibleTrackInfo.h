#ifndef _VISIBLETRACKINFO_H_

enum class ColumnType {
  UNKNOWN = 0,
  NOTE,
  VELOCITY,
  DELAY,
  EFFECT
};

class VisibleTrackInfo {
public:
  VisibleTrackInfo() { }

  int getColumnCount() const { return num_subtracks_ * ((has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0)) + (has_effect_column_ ? 1 : 0); }
  int getTrackWidth() const {
    return num_subtracks_ * ((has_note_column_ ? 4 : 0) + num_velocity_columns_ * 3 + (has_delay_column_ ? 3 : 0)) + (has_effect_column_ ? 5 : 0);
  }
  ColumnType getColumnType(int k) const {
    auto column_count = getColumnCount();
    if (has_effect_column_ && k == column_count - 1) {
      return ColumnType::EFFECT;
    } else {
      auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
      k = k % n;

      if (has_note_column_) {
	if (k == 0) return ColumnType::NOTE;
	else k--;
      }

      if (k < num_velocity_columns_) return ColumnType::VELOCITY;
      else k -= num_velocity_columns_;

      if (has_delay_column_) {
	if (k == 0) return ColumnType::DELAY;
	else k--;      
      }
      
      return ColumnType::UNKNOWN;
    }
  }
  bool isNoteColumn(int k) const { return getColumnType(k) == ColumnType::NOTE; }
  bool isVelocityColumn(int k) const { return getColumnType(k) == ColumnType::VELOCITY; }
  bool isDelayColumn(int k) const { return getColumnType(k) == ColumnType::DELAY; }
  bool isEffectColumn(int k) const { return getColumnType(k) == ColumnType::EFFECT; }
  
  int getNoteNumber(int k) const {
    auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
    return k / n;
  }

  void updateNumSubtracks(int n) {  
    if (n > num_subtracks_) num_subtracks_ = n;
  }
  
  int num_subtracks_ = 1;
  int num_velocity_columns_ = 0;
  bool has_note_column_ = true;
  bool has_delay_column_ = false;
  bool has_effect_column_ = false;
};

#endif
