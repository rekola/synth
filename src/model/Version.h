#ifndef _VERSION_H_
#define _VERSION_H_

// A (major, minor) content-identity pair for Song - major for *structural*
// changes (tracks, scenes, instruments, pattern length), minor for note/
// command/velocity/delay content edits that don't change the song's
// structure. Comparable as a whole via operator==/!=; Song exposes the two
// numbers separately too, for a consumer that only cares about one.
class Version {
 public:
  Version() = default;
  Version(int major, int minor) : major_(major), minor_(minor) { }

  int getMajor() const { return major_; }
  int getMinor() const { return minor_; }

  void incMajor() { major_++; }
  void incMinor() { minor_++; }

  bool operator==(const Version & other) const { return major_ == other.major_ && minor_ == other.minor_; }
  bool operator!=(const Version & other) const { return !(*this == other); }

 private:
  int major_ = 1; // matches Song::version_'s pre-existing default
  int minor_ = 0;
};

#endif
