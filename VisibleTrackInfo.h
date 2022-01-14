#ifndef _VISIBLETRACKINFO_H_

enum class ColumnType {
  NOTE = 1,
    VELOCITY,
    DELAY,
    EFFECT
};

class VisibleTrackInfo {
public:
  VisibleTrackInfo() { }

  size_t getColumnCount() const { return num_note_columns * (1 + (has_velocity_column ? 1 : 0) + (has_delay_column ? 1 : 0)) + (has_effect_column ? 1 : 0); }
  size_t getTrackWidth() const {
    return num_note_columns * (4 + (has_velocity_column ? 3 : 0) + (has_delay_column ? 3 : 0)) + (has_effect_column ? 5 : 0);
  }
  ColumnType getColumnType(size_t k) const {
    auto column_count = getColumnCount();
    if (has_effect_column && k == column_count - 1) {
      return ColumnType::EFFECT;
    } else {
      size_t n = 1 + (has_velocity_column ? 1 : 0) + (has_delay_column ? 1 : 0);

      if (has_velocity_column && k % n == 1) {
	return ColumnType::VELOCITY;
      } else if (has_delay_column && k % n == (n - 1)) {
	return ColumnType::DELAY;
      } else {
	return ColumnType::NOTE;
      }
    }
  }
  bool isNoteColumn(size_t k) const { return getColumnType(k) == ColumnType::NOTE; }
  bool isVelocityColumn(size_t k) const { return getColumnType(k) == ColumnType::VELOCITY; }
  bool isDelayColumn(size_t k) const { return getColumnType(k) == ColumnType::DELAY; }
  bool isEffectColumn(size_t k) const { return getColumnType(k) == ColumnType::EFFECT; }
  
  size_t getNoteNumber(size_t k) const {
    size_t n = 1 + (has_velocity_column ? 1 : 0) + (has_delay_column ? 1 : 0);
    return k / n;
  }
  
  size_t num_note_columns = 1;
  bool has_velocity_column = false;
  bool has_delay_column = false;
  bool has_effect_column = false;
};

#endif
