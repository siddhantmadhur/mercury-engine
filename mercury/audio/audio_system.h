/*
 * audio_system.h
 *
 * Plays sounds through miniaudio, the backend of Unity-style AudioSource
 * and AudioListener components. Clips are .wav, .ogg, .mp3 or .flac files,
 * read through os::ReadBytes (so packed release builds work) and decoded
 * once. Sources are referred to by id; MapScene keeps their positions and
 * the listener's in step with the scene.
 *
 * Without a sound device the system is "unavailable": every call is a
 * no-op, so games still run (headless tests, machines without audio).
 */
#ifndef MERCURY_AUDIO_AUDIO_SYSTEM_H
#define MERCURY_AUDIO_AUDIO_SYSTEM_H

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace Mercury {

class AudioSystem {
 public:
  using SourceId = uint32_t;
  // res:// path -> file path for the VFS.
  using Resolver = std::function<std::string(std::string_view)>;

  AudioSystem();
  ~AudioSystem();
  AudioSystem(const AudioSystem&) = delete;
  AudioSystem& operator=(const AudioSystem&) = delete;

  // Opens the default playback device (or, with `no_device`, mixes without
  // one, for tests). False if audio is unavailable.
  bool Initialize(Resolver resolver, bool no_device = false);
  bool IsAvailable() const { return engine_ != nullptr; }

  SourceId CreateSource();
  void DestroySource(SourceId id);

  // The source's main clip (AudioSource.clip); "" clears it.
  void SetClip(SourceId id, std::string_view res_path);
  void Play(SourceId id);
  void Stop(SourceId id);
  void Pause(SourceId id);
  void UnPause(SourceId id);
  // Plays a clip once over whatever the source plays, with the source's
  // pitch and volume times `volume_scale` (AudioSource.PlayOneShot).
  void PlayOneShot(SourceId id, std::string_view res_path, float volume_scale = 1.0f);
  // The main clip or a one-shot is playing (AudioSource.isPlaying).
  bool IsPlaying(SourceId id) const;
  // Seconds into the main clip.
  float GetTime(SourceId id) const;

  void SetVolume(SourceId id, float volume);
  void SetPitch(SourceId id, float pitch);
  void SetLoop(SourceId id, bool loop);
  void SetMute(SourceId id, bool mute);
  // spatial_blend 0 is 2D (no panning or fading), 1 is fully 3D.
  void SetSpatial(SourceId id, float spatial_blend, float min_distance, float max_distance);
  void SetPosition(SourceId id, glm::vec3 position);

  void SetListener(glm::vec3 position, glm::vec3 forward, glm::vec3 up);
  // Pauses everything (AudioListener.pause).
  void SetPaused(bool paused);
  void SetMasterVolume(float volume);

  // Frees finished one-shots; call once per frame.
  void Update();

  // Length of a clip in seconds (0 if it cannot be read).
  float GetClipLength(std::string_view res_path);

 private:
  struct Sound;
  struct Source;

  Sound* NewSound(std::string_view res_path);
  void Apply(Source& source, Sound& sound) const;

  struct Engine;
  std::unique_ptr<Engine> engine_;
  Resolver resolver_;
  std::map<SourceId, std::unique_ptr<Source>> sources_;
  SourceId next_id_ = 1;
  bool paused_ = false;
};

}  // namespace Mercury

#endif  // MERCURY_AUDIO_AUDIO_SYSTEM_H
