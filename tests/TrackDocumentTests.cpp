#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/instruments/GenericInstrument.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/Oscillator.h"
#include "../src/model/Group.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/Song.h"
#include "../src/model/TrackNodes.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

namespace {

std::string readWholeFile(const std::string & path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

const LeafTrack & leaf(const Song & song, int track_id) {
  return dynamic_cast<const LeafTrack &>(*song.getMasterTrack().getChildByInternalId(track_id));
}

} // namespace

TEST(adding_a_track_is_one_undoable_edit_and_keeps_the_object_it_was_given) {
  Song song;
  auto & journal = song.document().journal();
  auto before = journal.size();
  auto track = std::make_unique<InstrumentTrack>(0);
  auto * given = track.get();
  auto & added = song.addTrack(std::move(track));
  CHECK(&added == given); // the same object, now shared and read-only
  CHECK(song.getMasterTrack().getChildren().size() == 1);
  CHECK(song.getMasterTrack().getChildren()[0].get() == given);
  CHECK(journal.size() == before + 1);
  CHECK(journal.back().label == "add track");
  CHECK(!given->getId().empty()); // got a textual id
}

TEST(a_removed_track_comes_back_as_the_same_object_when_the_edit_is_undone) {
  Song song;
  auto id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  auto * object = song.getMasterTrack().getChildByInternalId(id);
  CHECK(song.removeTrack(id));
  CHECK(song.getMasterTrack().getChildren().empty());
  CHECK(!song.removeTrack(id)); // already gone

  song.document().apply(doc::Document::inverse(song.document().journal().back()));
  CHECK(song.getMasterTrack().getChildren().size() == 1);
  CHECK(song.getMasterTrack().getChildByInternalId(id) == object); // nothing was rebuilt
}

TEST(an_edit_to_a_track_writes_its_node_and_replaces_only_the_objects_that_changed) {
  Song song;
  auto first = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  auto second = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  auto * first_before = song.getMasterTrack().getChildByInternalId(first);
  auto * second_before = song.getMasterTrack().getChildByInternalId(second);
  auto * master_before = &song.getMasterTrack();
  auto journal_size = song.document().journal().size();

  CHECK(song.editTrack(first, [](Track & track) { static_cast<LeafTrack &>(track).setMuted(true); }));

  CHECK(leaf(song, first).isMuted());
  CHECK(!leaf(song, second).isMuted());
  CHECK(song.document().journal().size() == journal_size + 1);
  CHECK(song.getMasterTrack().getChildByInternalId(first) != first_before);  // rebuilt
  CHECK(song.getMasterTrack().getChildByInternalId(second) == second_before); // shared
  CHECK(&song.getMasterTrack() != master_before); // its child list changed
  CHECK(song.getMasterTrack().getChildByInternalId(first)->getInternalId() == first); // same identity
}

TEST(an_edit_that_changes_nothing_is_not_an_edit) {
  Song song;
  auto id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  auto size = song.document().journal().size();
  auto version = song.getVersion();
  CHECK(song.editTrack(id, [](Track & track) { static_cast<LeafTrack &>(track).setMuted(false); }));
  CHECK(song.document().journal().size() == size);
  CHECK(song.getVersion() == version);
  CHECK(!song.editTrack(999999, [](Track &) { }));
}

TEST(editing_a_track_leaves_attributes_nothing_recognizes_alone) {
  Song song;
  auto id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  // A node as a newer build might have written it.
  Song::Edit edit(song, "future attribute");
  auto & document = song.document();
  doc::NodeId node = doc::kNoNode;
  document.forEachNode([&](const doc::Node & n) {
    if (auto iid = n.find(tracknodes::kIidKey); iid && std::get<int64_t>(*iid) == id) node = n.id;
  });
  CHECK(node != doc::kNoNode);
  document.setProperty(node, "futureThing", doc::Value(std::string("42")));
  song.editTrack(id, [](Track & track) { static_cast<LeafTrack &>(track).setMuted(true); });
  CHECK(std::get<std::string>(*document.get(node)->find("futureThing")) == "42");
  CHECK(document.get(node)->find("mute") != nullptr);
  song.editTrack(id, [](Track & track) { static_cast<LeafTrack &>(track).setMuted(false); });
  CHECK(document.get(node)->find("mute") == nullptr); // back at its default, so not stored
  CHECK(std::get<std::string>(*document.get(node)->find("futureThing")) == "42");
}

TEST(a_track_edit_undoes_through_the_documents_inverse) {
  Song song;
  auto id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  song.editTrack(id, [](Track & track) { track.setSendA(0.5f); static_cast<LeafTrack &>(track).setAzimuth(30.0f); });
  CHECK_NEAR(leaf(song, id).getSends().a, 0.5f, 1e-4f);
  CHECK_NEAR(leaf(song, id).getAzimuth(), 30.0f, 1e-4f);
  song.document().apply(doc::Document::inverse(song.document().journal().back()));
  CHECK(leaf(song, id).getSends().a == 0.0f);
  CHECK(leaf(song, id).getAzimuth() == 0.0f);
}

TEST(a_track_added_with_children_keeps_them_and_a_sibling_lands_inside_a_group) {
  Song song;
  auto group = std::make_unique<Group>();
  auto & inner = group->addChild(std::make_unique<InstrumentTrack>(0));
  auto inner_id = inner.getInternalId();
  song.addTrack(std::move(group));
  auto & sibling = song.addTrack(std::make_unique<InstrumentTrack>(0), inner_id);
  auto & compiled_group = *song.getMasterTrack().getChildren()[0];
  CHECK(compiled_group.getChildren().size() == 2);
  CHECK(compiled_group.getChildren()[0]->getInternalId() == inner_id);
  CHECK(compiled_group.getChildren()[1].get() == &sibling);
}

TEST(instruments_and_the_default_kit_are_nodes_too) {
  Song song;
  song.addInstrument(std::make_unique<Oscillator>(WaveformType::SINE));
  song.addInstrument(std::make_unique<Oscillator>(WaveformType::SAW));
  auto * second = &song.getInstrumentPool().getInstrument(1);
  CHECK(song.getInstrumentPool().getInstruments().size() == 2);

  song.removeInstrument(0);
  CHECK(song.getInstrumentPool().getInstruments().size() == 1);
  CHECK(&song.getInstrumentPool().getInstrument(0) == second); // not rebuilt

  song.document().apply(doc::Document::inverse(song.document().journal().back()));
  CHECK(song.getInstrumentPool().getInstruments().size() == 2);
  CHECK(&song.getInstrumentPool().getInstrument(1) == second);
}

TEST(the_bus_slots_are_nodes_and_a_change_undoes) {
  Song song;
  CHECK(song.getBusSlotKind(0) == BusEffectKind::Reverb);
  CHECK(song.getBusSlotKind(1) == BusEffectKind::Delay);
  song.setBusSlotKind(1, BusEffectKind::Haze);
  CHECK(song.getBusSlotKind(1) == BusEffectKind::Haze);
  song.document().apply(doc::Document::inverse(song.document().journal().back()));
  CHECK(song.getBusSlotKind(1) == BusEffectKind::Delay);
}

TEST(the_published_content_carries_the_tracks_it_was_made_with) {
  Song song;
  song.setContentPublished(true);
  CHECK(song.readContent()->tracks->master->getChildren().empty());
  auto id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  {
    auto content = song.readContent();
    CHECK(content->tracks->master->getChildren().size() == 1);
    CHECK(content->tracks.get() == song.compiledTracks().get());
  }
  auto generation = song.readContent()->tracks->generation;
  song.editTrack(id, [](Track & track) { static_cast<LeafTrack &>(track).setSolo(true); });
  auto content = song.readContent();
  CHECK(content->tracks->generation > generation);
  CHECK(dynamic_cast<const LeafTrack &>(*content->tracks->master->getChildByInternalId(id)).isSolo());
  // A song change that touches no track compiles none.
  auto compiled = song.compiledTracks();
  song.setTempo(99);
  CHECK(song.compiledTracks() == compiled);
}

TEST(a_loaded_song_saves_the_same_after_its_tracks_pass_through_the_document) {
  namespace fs = std::filesystem;
  auto scratch = (fs::path(TESTS_SCRATCH_DIR) / "track_document_round_trip.xml").string();
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/percussion_track.xml", provider));
  song.save(scratch);
  Song again;
  CHECK(again.open(scratch, provider));
  auto scratch2 = (fs::path(TESTS_SCRATCH_DIR) / "track_document_round_trip2.xml").string();
  again.save(scratch2);
  CHECK(readWholeFile(scratch) == readWholeFile(scratch2));
  CHECK(readWholeFile(scratch).find("<percussionTrack") != std::string::npos);
  fs::remove(scratch);
  fs::remove(scratch2);
}

TEST(a_generic_instruments_generator_overrides_round_trip_through_its_node) {
  namespace fs = std::filesystem;
  auto path = (fs::path(TESTS_SCRATCH_DIR) / "track_document_generators.xml").string();
  {
    std::ofstream out(path);
    out << "<?xml version=\"1.0\"?><song><instruments><instrument from=\"piano\"><generator name=\"attackVolEnv\" value=\"-2000\"/>"
           "<generator name=\"someFutureGenerator\" value=\"3\"/></instrument></instruments><tracks/></song>";
  }
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(path, provider));
  auto & instrument = dynamic_cast<const GenericInstrument &>(song.getInstrumentPool().getInstrument(0));
  CHECK(instrument.getGeneratorOverrides().size() == 1);
  CHECK(instrument.getUnknownGeneratorOverrides().size() == 1);
  // And again, built from the node alone this time (an edit that replaces the
  // pool entry's object would do the same).
  song.document().forEachNode([](const doc::Node & node) { CHECK(node.type != "oscillator"); });
  fs::remove(path);
}

// Sends are stored as dB text and held as linear gains, and that round trip is
// not exact in floating point: a track handed in as an object has to equal
// the one rebuilt from its node, or an unrelated edit would change its level.
TEST(a_track_added_as_an_object_equals_the_one_rebuilt_from_its_node) {
  Song song;
  auto track = std::make_unique<InstrumentTrack>(0);
  track->setSendA(sendDbToLinear(-59.9726f)); // one of the levels that does not survive dB text and back
  track->setSendB(sendDbToLinear(-12.3456f));
  auto id = song.addTrack(std::move(track)).getInternalId();
  auto added = song.getMasterTrack().getChildByInternalId(id)->getSends();

  song.editTrack(id, [](Track & edited) { edited.setCollapsed(true); });
  song.editTrack(id, [](Track & edited) { edited.setCollapsed(false); });
  auto rebuilt = song.getMasterTrack().getChildByInternalId(id)->getSends();
  CHECK(rebuilt.a == added.a);
  CHECK(rebuilt.b == added.b);
  CHECK(rebuilt.main == added.main);
}
