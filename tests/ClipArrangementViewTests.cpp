#include "TestFramework.h"

#include "../src/model/ArrangementView.h"
#include "../src/model/ClipView.h"

#include <random>

namespace {

struct Score {
  doc::Document document;
  SampleStore samples;
  ScoreContext context{ &document, &samples };
  Score() { document.setVerify(true); }
};

std::shared_ptr<AudioBuffer> makeBuffer(int frames, float level) {
  auto buffer = std::make_shared<AudioBuffer>(1, frames);
  for (int i = 0; i < frames; i++) buffer->getChannelData(0)[i] = level * static_cast<float>(i % 7);
  return buffer;
}

bool samePattern(const Pattern & a, const Pattern & b) {
  if (a.getLength() != b.getLength()) return false;
  if (a.getNotesByRow().size() != b.getNotesByRow().size() || a.getCommandsByRow().size() != b.getCommandsByRow().size()) return false;
  for (auto & [ row, notes ] : a.getNotesByRow()) {
    auto & other = b.getNotes(row);
    if (other.size() != notes.size()) return false;
    for (size_t i = 0; i < notes.size(); i++) {
      if (notes[i].isDefined() != other[i].isDefined()) return false;
      if (notes[i].isDefined() && (notes[i].getValue() != other[i].getValue() || notes[i].getVelocity() != other[i].getVelocity() || notes[i].getDelay() != other[i].getDelay())) return false;
    }
  }
  for (auto & [ row, commands ] : a.getCommandsByRow()) {
    auto & other = b.getCommandsAt(row);
    if (other.size() != commands.size()) return false;
    for (size_t i = 0; i < commands.size(); i++) if (to_string(commands[i]) != to_string(other[i])) return false;
  }
  return true;
}

bool sameSample(const SampleContent & a, const SampleContent & b) {
  return a.getBuffer() == b.getBuffer() && a.getInPoint() == b.getInPoint() && a.getOutPoint() == b.getOutPoint() &&
    a.getOriginalTempo() == b.getOriginalTempo() && a.getNativeSampleRate() == b.getNativeSampleRate();
}

bool sameClip(const Clip & a, const Clip & b) {
  if (a.getId() != b.getId() || a.getName() != b.getName() || a.getLeafTrackId() != b.getLeafTrackId() || a.getLength() != b.getLength() ||
      a.isLooping() != b.isLooping() || a.hasStopButton() != b.hasStopButton() || a.isEmpty() != b.isEmpty() || a.hasSample() != b.hasSample()) return false;
  if (!samePattern(a.getLeafPattern(), b.getLeafPattern())) return false;
  if (a.getSampleLayers().size() != b.getSampleLayers().size()) return false;
  for (size_t i = 0; i < a.getSampleLayers().size(); i++) if (!sameSample(a.getSampleLayers()[i], b.getSampleLayers()[i])) return false;
  return true;
}

}  // namespace

TEST(clip_view_matches_the_value_clip_under_random_edits) {
  std::mt19937 rng(99);
  Score score;
  Clip reference(5);
  auto node = ClipView::create(score.context, 5);
  score.document.insertChild(score.document.root(), ClipList::slotName(5), 0, node);
  ClipView view(score.context, node);
  std::vector<std::shared_ptr<AudioBuffer> > buffers = { makeBuffer(64, 0.1f), makeBuffer(96, 0.2f), makeBuffer(32, 0.3f) };

  for (int step = 0; step < 1500; step++) {
    switch (rng() % 9) {
      case 0: { std::string id = "clip" + std::to_string(rng() % 5); reference.setId(id); view.setId(id); break; }
      case 1: { std::string name = rng() % 3 ? "take " + std::to_string(rng() % 9) : ""; reference.setName(name); view.setName(name); break; }
      case 2: { int length = static_cast<int>(rng() % 40); reference.setLength(length); view.setLength(length); break; }
      case 3: { bool b = rng() % 2; reference.setLooping(b); view.setLooping(b); break; }
      case 4: { bool b = rng() % 2; reference.setStopButton(b); view.setStopButton(b); break; }
      case 5: {
        int row = static_cast<int>(rng() % 12), column = static_cast<int>(rng() % 3);
        Note note(static_cast<int>(rng() % 30), static_cast<short>(rng() % 100));
        reference.getLeafPattern().setNote(row, column, note);
        view.getLeafPattern().setNote(row, column, note);
        break;
      }
      case 6:
        if (reference.getSampleLayers().size() < 3) {
          auto & layer = reference.addSampleLayer();
          auto buffer = buffers[rng() % buffers.size()];
          layer.setBuffer(buffer);
          layer.setNativeSampleRate(44100);
          layer.setOriginalTempo(static_cast<short>(100 + rng() % 40));
          layer.setInPoint(static_cast<float>(rng() % 3) * 0.1f);
          auto added = view.addSampleLayer();
          added.setBuffer(buffer);
          added.setNativeSampleRate(layer.getNativeSampleRate());
          added.setOriginalTempo(layer.getOriginalTempo());
          added.setInPoint(layer.getInPoint());
        }
        break;
      case 7:
        if (!reference.getSampleLayers().empty()) {
          auto index = rng() % reference.getSampleLayers().size();
          float out = static_cast<float>(rng() % 4) * 0.05f;
          reference.getSampleLayers()[index].setOutPoint(out);
          view.sampleLayer(index).setOutPoint(out);
        }
        break;
      default: {
        // Round trip through the value form.
        auto copy = view.toClip();
        CHECK(sameClip(copy, reference));
        break;
      }
    }
    if (!sameClip(view.toClip(), reference)) {
      std::fprintf(stderr, "  clip diverged at step %d\n", step);
      CHECK(false);
      return;
    }
  }
}

TEST(clip_view_assign_replaces_content_and_is_undone_as_one_step) {
  Score score;
  auto node = ClipView::create(score.context, 3);
  score.document.insertChild(score.document.root(), ClipList::slotName(3), 0, node);
  ClipView view(score.context, node);
  view.setName("old");
  view.getLeafPattern().setNote(1, 0, Note(10, 50));

  Clip source(3);
  source.setId("clip9");
  source.setName("new");
  source.setLength(16);
  source.getLeafPattern().setNote(4, 0, Note(20, 60));
  source.addSampleLayer().setBuffer(makeBuffer(40, 0.5f));

  auto before = score.document.dump();
  auto entries = score.document.journal().size();
  view.assign(source);
  CHECK(score.document.journal().size() == entries + 1);
  CHECK(sameClip(view.toClip(), source));
  score.document.apply(doc::Document::inverse(score.document.journal().back()));
  CHECK(score.document.dump() == before);
}

TEST(clip_list_keeps_scene_order_and_a_hole_is_a_real_empty_clip) {
  Score score;
  for (int i = 0; i < 3; i++) {
    auto node = ClipView::create(score.context, 7);
    score.document.insertChild(score.document.root(), ClipList::slotName(7), static_cast<size_t>(i), node);
    ClipView(score.context, node).setName("c" + std::to_string(i));
  }
  ClipList list(score.context, 7);
  CHECK(list.size() == 3);
  CHECK(list[1].getName() == "c1");
  CHECK(list.back().getName() == "c2");
  int count = 0;
  for (auto clip : list) { CHECK(clip.isEmpty()); count++; }
  CHECK(count == 3);
  CHECK(ClipList(score.context, 8).empty());
}

TEST(multi_layer_clip_mixes_only_after_a_rebuild_and_falls_back_to_the_first_layer) {
  Score score;
  auto node = ClipView::create(score.context, 1);
  score.document.insertChild(score.document.root(), ClipList::slotName(1), 0, node);
  ClipView view(score.context, node);
  Clip reference(1);
  for (auto frames : { 64, 96 }) {
    auto buffer = makeBuffer(frames, 0.25f);
    auto layer = view.addSampleLayer();
    layer.setBuffer(buffer);
    layer.setNativeSampleRate(44100);
    layer.setOriginalTempo(120);
    auto & ref = reference.addSampleLayer();
    ref.setBuffer(buffer);
    ref.setNativeSampleRate(44100);
    ref.setOriginalTempo(120);
  }
  // Not rebuilt yet: the first layer alone.
  CHECK(view.getMixedContent().getBuffer() == view.sampleLayer(0).content().getBuffer());

  view.rebuildMixedContent(44100, 120);
  reference.rebuildMixedContent(44100, 120);
  auto & mixed = view.getMixedContent();
  auto & expected = reference.getMixedContent();
  CHECK(mixed.getBuffer() && expected.getBuffer());
  CHECK(mixed.getBuffer()->numberOfFrames() == expected.getBuffer()->numberOfFrames());
  for (int i = 0; i < mixed.getBuffer()->numberOfFrames(); i++) CHECK_NEAR(mixed.getBuffer()->getChannelData(0)[i], expected.getBuffer()->getChannelData(0)[i], 1e-6f);

  // A layer edit makes the mix stale again.
  view.sampleLayer(1).setInPoint(0.001f);
  CHECK(view.getMixedContent().getBuffer() == view.sampleLayer(0).content().getBuffer());
}

TEST(sample_content_is_stable_until_the_layer_changes) {
  Score score;
  auto node = ClipView::create(score.context, 1);
  score.document.insertChild(score.document.root(), ClipList::slotName(1), 0, node);
  ClipView view(score.context, node);
  auto layer = view.firstSampleLayer();
  layer.setBuffer(makeBuffer(50, 0.1f));
  layer.setOriginalTempo(120);
  auto first_identity = layer.content().identity();
  CHECK(first_identity != 0);
  CHECK(layer.content().identity() == first_identity); // same object while nothing changes
  layer.content().setStretchedBuffer(makeBuffer(10, 0.2f), 100);
  CHECK(layer.content().getStretchedBuffer(100) != nullptr); // the audio side's work is kept
  layer.setOriginalTempo(130);
  CHECK(layer.content().identity() != first_identity);
  CHECK(layer.content().getStretchedBuffer(100) == nullptr);
}

TEST(arrangement_view_matches_the_value_arrangement_under_random_edits) {
  std::mt19937 rng(2024);
  Score score;
  auto node = ArrangementView::create(score.document);
  score.document.insertChild(score.document.root(), scoreschema::kArrangementSlot, 0, node);
  ArrangementView view(score.context, node);
  Arrangement reference;

  for (int step = 0; step < 3000; step++) {
    int track = 1 + static_cast<int>(rng() % 3), row = static_cast<int>(rng() % 20), column = static_cast<int>(rng() % 3);
    switch (rng() % 12) {
      case 0: case 1: { Note n(static_cast<int>(rng() % 30), static_cast<short>(1 + rng() % 100)); reference.setNote(row, track, column, n); view.setNote(row, track, column, n); break; }
      case 2: { std::vector<Note> notes = { Note(static_cast<int>(rng() % 30), 40), Note() , Note(5, 20) }; reference.setNotes(row, track, notes); view.setNotes(row, track, notes); break; }
      case 3: { Note n(7, 70); CHECK(reference.pushNote(row, track, n) == view.pushNote(row, track, n)); break; }
      case 4: reference.clearNotes(row, track); view.clearNotes(row, track); break;
      case 5: reference.deleteNote(row, track, column); view.deleteNote(row, track, column); break;
      case 6: { int rows = 6 + static_cast<int>(rng() % 10); reference.insertRowForTrack(track, row, rows); view.insertRowForTrack(track, row, rows); break; }
      case 7: { Command c("0U10"); reference.setCommand(row, track, column, c); view.setCommand(row, track, column, c); break; }
      case 8: { std::string clip = rng() % 4 ? "clip" + std::to_string(rng() % 3) : "OFF"; reference.setInstance(track, row, clip); view.setInstance(track, row, clip); break; }
      case 9: reference.clearInstance(track, row); view.clearInstance(track, row); break;
      case 10: { Pattern p; p.setNote(row, 0, Note(9, 99)); p.setLength(8); reference.setPatternForTrack(track, p); view.setPatternForTrack(track, p); break; }
      default: break;
    }
    auto got = view.toArrangement();
    bool ok = got.getPatternsByTrack().size() == reference.getPatternsByTrack().size() && got.getInstancesByTrack() == reference.getInstancesByTrack();
    for (auto & [ id, pattern ] : reference.getPatternsByTrack()) {
      auto it = got.getPatternsByTrack().find(id);
      ok = ok && it != got.getPatternsByTrack().end() && samePattern(pattern, it->second);
    }
    if (!ok) {
      std::fprintf(stderr, "  arrangement diverged at step %d\n", step);
      CHECK(false);
      return;
    }
    // Point reads.
    CHECK(view.getInstance(track, row) == reference.getInstance(track, row));
    CHECK(view.getNotes(row, track).size() == reference.getNotes(row, track).size());
    CHECK(view.getEffectiveRow(track, row, 8) == reference.getEffectiveRow(track, row, 8));
  }
}

TEST(arrangement_view_background_bed_replace_and_undo) {
  Score score;
  auto node = ArrangementView::create(score.document);
  score.document.insertChild(score.document.root(), scoreschema::kArrangementSlot, 0, node);
  ArrangementView view(score.context, node);
  CHECK(view.getSampleBackgroundContent(2) == nullptr);

  SampleContent bed;
  bed.setBuffer(makeBuffer(80, 0.1f));
  bed.setNativeSampleRate(44100);
  view.setSampleBackground(2, bed);
  CHECK(view.getSampleBackgroundContent(2) != nullptr);
  auto first = view.getSampleBackgroundContent(2)->getBuffer();

  SampleContent bed2;
  bed2.setBuffer(makeBuffer(120, 0.2f));
  bed2.setNativeSampleRate(44100);
  view.setSampleBackground(2, bed2);
  CHECK(view.getSampleBackgroundContent(2)->getBuffer()->numberOfFrames() == 120);
  score.document.apply(doc::Document::inverse(score.document.journal().back()));
  CHECK(view.getSampleBackgroundContent(2)->getBuffer() == first);
  CHECK(view.getSampleBackgroundsByTrack().size() == 1);
}
