#ifndef _ARRANGEMENTPATTERNSOURCE_H_
#define _ARRANGEMENTPATTERNSOURCE_H_

#include "PatternSource.h"

class Controller;
class Song;

// The arrangement: one block, its rows the timeline's absolute rows; the
// cursor row is the transport's edit position, and a cell shows whatever
// actually plays there - a placed clip instance's notes, or the track's
// background. Block operations act on the content the anchor row shows,
// per track (ArrangementRegionGrid); effect commands always live on the
// background, where playback reads them.
class ArrangementPatternSource : public PatternSource {
 public:
  explicit ArrangementPatternSource(Controller & controller) : controller_(controller) { }

  RowAddress cursor() const override;
  void moveCursor(int delta_rows) override;

  RowAddress normalize(int block, int row) const override;
  int blockCount() const override { return 1; }
  int blockLength(int block) const override;

  ReadTarget read(int track_id, RowAddress address) const override;
  EditTarget edit(int track_id, RowAddress address) override;
  std::unique_ptr<const PatternGrid> readGrid(RowAddress anchor) const override;
  std::unique_ptr<PatternGrid> editGrid(RowAddress anchor, bool create) override;
  std::pair<int, int> sourceRows(int track_id, RowAddress anchor) const override;

  void collectTrackInfo(RowAddress first, int rows, std::unordered_map<int, VisibleTrackInfo> & track_info) const override;

  std::optional<int> locatorRow(RowAddress address) const override;

  void insertRow(int track_id, RowAddress address) override;
  bool hasInstance(int track_id, RowAddress address) const override;
  bool stopInstance(int track_id, RowAddress address) override;
  const SampleContent * sampleBackground(int track_id, int block) const override;
  std::optional<int> playheadRow(int, int) const override { return std::nullopt; }
  bool showsClipIndirection() const override { return true; }
  bool hasLocators() const override { return true; }
  bool cursorFollowsTransport() const override { return true; }

 private:
  // Always the active buffer's song - it changes when the buffer does.
  Song & song() const;

  Controller & controller_;
};

#endif
