#include "audio_system.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "debugger/debugger.h"
#include "miniaudio/miniaudio.h"
#include "os/filesystem.h"

namespace Mercury {

namespace {

// miniaudio's VFS over os::ReadBytes: files are read whole into memory.
struct MemoryFile {
  std::vector<uint8_t> data;
  size_t cursor = 0;
};

struct Vfs {
  ma_vfs_callbacks callbacks;  // must come first
};

ma_result VfsOpen(ma_vfs*, const char* path, ma_uint32 mode, ma_vfs_file* file) {
  if (mode & MA_OPEN_MODE_WRITE) return MA_ACCESS_DENIED;
  auto bytes = os::ReadBytes(path);
  if (!bytes) return MA_DOES_NOT_EXIST;
  auto* memory = new MemoryFile{std::move(*bytes), 0};
  *file = memory;
  return MA_SUCCESS;
}

ma_result VfsOpenW(ma_vfs*, const wchar_t*, ma_uint32, ma_vfs_file*) {
  return MA_NOT_IMPLEMENTED;
}

ma_result VfsClose(ma_vfs*, ma_vfs_file file) {
  delete static_cast<MemoryFile*>(file);
  return MA_SUCCESS;
}

ma_result VfsRead(ma_vfs*, ma_vfs_file file, void* dst, size_t bytes, size_t* read) {
  auto* memory = static_cast<MemoryFile*>(file);
  const size_t available = memory->data.size() - memory->cursor;
  const size_t count = std::min(bytes, available);
  std::memcpy(dst, memory->data.data() + memory->cursor, count);
  memory->cursor += count;
  if (read) *read = count;
  return count == 0 && bytes > 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result VfsWrite(ma_vfs*, ma_vfs_file, const void*, size_t, size_t*) {
  return MA_ACCESS_DENIED;
}

ma_result VfsSeek(ma_vfs*, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin) {
  auto* memory = static_cast<MemoryFile*>(file);
  ma_int64 target = offset;
  if (origin == ma_seek_origin_current) target = static_cast<ma_int64>(memory->cursor) + offset;
  if (origin == ma_seek_origin_end) target = static_cast<ma_int64>(memory->data.size()) + offset;
  if (target < 0 || target > static_cast<ma_int64>(memory->data.size())) return MA_BAD_SEEK;
  memory->cursor = static_cast<size_t>(target);
  return MA_SUCCESS;
}

ma_result VfsTell(ma_vfs*, ma_vfs_file file, ma_int64* cursor) {
  *cursor = static_cast<ma_int64>(static_cast<MemoryFile*>(file)->cursor);
  return MA_SUCCESS;
}

ma_result VfsInfo(ma_vfs*, ma_vfs_file file, ma_file_info* info) {
  info->sizeInBytes = static_cast<MemoryFile*>(file)->data.size();
  return MA_SUCCESS;
}

}  // namespace

struct AudioSystem::Engine {
  Vfs vfs;
  ma_resource_manager resource_manager;
  ma_engine engine;
  bool resource_manager_ready = false;
};

struct AudioSystem::Sound {
  ma_sound sound;
};

struct AudioSystem::Source {
  std::string clip;
  std::unique_ptr<Sound> main;
  std::list<std::unique_ptr<Sound>> one_shots;
  std::list<float> one_shot_scales;
  float volume = 1.0f;
  float pitch = 1.0f;
  bool loop = false;
  bool mute = false;
  float spatial_blend = 0.0f;
  float min_distance = 1.0f;
  float max_distance = 500.0f;
  glm::vec3 position = glm::vec3(0.0f);
};

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
  for (auto& [id, source] : sources_) {
    if (source->main) ma_sound_uninit(&source->main->sound);
    for (auto& shot : source->one_shots) ma_sound_uninit(&shot->sound);
  }
  sources_.clear();
  if (engine_) {
    ma_engine_uninit(&engine_->engine);
    if (engine_->resource_manager_ready) ma_resource_manager_uninit(&engine_->resource_manager);
  }
}

bool AudioSystem::Initialize(Resolver resolver, bool no_device) {
  resolver_ = std::move(resolver);
  auto engine = std::make_unique<Engine>();
  engine->vfs.callbacks = {VfsOpen, VfsOpenW, VfsClose, VfsRead,
                           VfsWrite, VfsSeek, VfsTell, VfsInfo};

  ma_resource_manager_config rm_config = ma_resource_manager_config_init();
  rm_config.pVFS = &engine->vfs;
  rm_config.decodedFormat = ma_format_f32;
  // Decoding happens on the calling thread; clips here are short.
  rm_config.jobThreadCount = 0;
  rm_config.flags = MA_RESOURCE_MANAGER_FLAG_NO_THREADING;
  if (ma_resource_manager_init(&rm_config, &engine->resource_manager) != MA_SUCCESS) {
    PDebug::warn("Audio: could not create the resource manager; sound is off");
    return false;
  }
  engine->resource_manager_ready = true;

  ma_engine_config config = ma_engine_config_init();
  config.pResourceManager = &engine->resource_manager;
  if (no_device) {
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = 48000;
  }
  if (ma_engine_init(&config, &engine->engine) != MA_SUCCESS) {
    PDebug::warn("Audio: no playback device; sound is off");
    ma_resource_manager_uninit(&engine->resource_manager);
    return false;
  }
  ma_engine_listener_set_world_up(&engine->engine, 0, 0.0f, 1.0f, 0.0f);
  engine_ = std::move(engine);
  return true;
}

AudioSystem::SourceId AudioSystem::CreateSource() {
  const SourceId id = next_id_++;
  sources_[id] = std::make_unique<Source>();
  return id;
}

void AudioSystem::DestroySource(SourceId id) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  if (it->second->main) ma_sound_uninit(&it->second->main->sound);
  for (auto& shot : it->second->one_shots) ma_sound_uninit(&shot->sound);
  sources_.erase(it);
}

AudioSystem::Sound* AudioSystem::NewSound(std::string_view res_path) {
  if (!engine_ || res_path.empty()) return nullptr;
  const std::string file = resolver_ ? resolver_(res_path) : std::string(res_path);
  auto sound = std::make_unique<Sound>();
  const ma_uint32 flags = MA_SOUND_FLAG_DECODE;
  if (ma_sound_init_from_file(&engine_->engine, file.c_str(), flags, nullptr, nullptr,
                              &sound->sound) != MA_SUCCESS) {
    PDebug::warn("Audio: could not load {}", res_path);
    return nullptr;
  }
  return sound.release();
}

void AudioSystem::Apply(Source& source, Sound& sound) const {
  ma_sound_set_volume(&sound.sound, source.mute ? 0.0f : source.volume);
  ma_sound_set_pitch(&sound.sound, std::max(source.pitch, 0.01f));
  const bool spatial = source.spatial_blend > 0.5f;
  ma_sound_set_spatialization_enabled(&sound.sound, spatial ? MA_TRUE : MA_FALSE);
  if (spatial) {
    // Unity's default rolloff is logarithmic between min and max distance.
    ma_sound_set_attenuation_model(&sound.sound, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(&sound.sound, source.min_distance);
    ma_sound_set_max_distance(&sound.sound, source.max_distance);
    ma_sound_set_position(&sound.sound, source.position.x, source.position.y,
                          source.position.z);
  }
}

void AudioSystem::SetClip(SourceId id, std::string_view res_path) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  Source& source = *it->second;
  if (source.clip == res_path && source.main) return;
  if (source.main) ma_sound_uninit(&source.main->sound);
  source.main.reset();
  source.clip = std::string(res_path);
  if (Sound* sound = NewSound(res_path)) {
    source.main.reset(sound);
    ma_sound_set_looping(&sound->sound, source.loop ? MA_TRUE : MA_FALSE);
    Apply(source, *sound);
  }
}

void AudioSystem::Play(SourceId id) {
  auto it = sources_.find(id);
  if (it == sources_.end() || !it->second->main) return;
  ma_sound& sound = it->second->main->sound;
  ma_sound_seek_to_pcm_frame(&sound, 0);
  ma_sound_start(&sound);
}

void AudioSystem::Stop(SourceId id) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  if (it->second->main) {
    ma_sound_stop(&it->second->main->sound);
    ma_sound_seek_to_pcm_frame(&it->second->main->sound, 0);
  }
  // Unity's Stop also cuts one-shots short.
  for (auto& shot : it->second->one_shots) ma_sound_stop(&shot->sound);
}

void AudioSystem::Pause(SourceId id) {
  auto it = sources_.find(id);
  if (it != sources_.end() && it->second->main) ma_sound_stop(&it->second->main->sound);
}

void AudioSystem::UnPause(SourceId id) {
  auto it = sources_.find(id);
  if (it != sources_.end() && it->second->main) ma_sound_start(&it->second->main->sound);
}

void AudioSystem::PlayOneShot(SourceId id, std::string_view res_path, float volume_scale) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  Source& source = *it->second;
  Sound* sound = NewSound(res_path);
  if (!sound) return;
  Apply(source, *sound);
  ma_sound_set_volume(&sound->sound, source.mute ? 0.0f : source.volume * volume_scale);
  ma_sound_start(&sound->sound);
  source.one_shots.emplace_back(sound);
  source.one_shot_scales.push_back(volume_scale);
}

bool AudioSystem::IsPlaying(SourceId id) const {
  auto it = sources_.find(id);
  if (it == sources_.end()) return false;
  const Source& source = *it->second;
  if (source.main && ma_sound_is_playing(&source.main->sound)) return true;
  for (const auto& shot : source.one_shots) {
    if (ma_sound_is_playing(&shot->sound) && !ma_sound_at_end(&shot->sound)) return true;
  }
  return false;
}

float AudioSystem::GetTime(SourceId id) const {
  auto it = sources_.find(id);
  if (it == sources_.end() || !it->second->main) return 0.0f;
  float cursor = 0.0f;
  ma_sound_get_cursor_in_seconds(&it->second->main->sound, &cursor);
  return cursor;
}

void AudioSystem::SetVolume(SourceId id, float volume) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  it->second->volume = volume;
  if (it->second->main) Apply(*it->second, *it->second->main);
}

void AudioSystem::SetPitch(SourceId id, float pitch) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  it->second->pitch = pitch;
  if (it->second->main) Apply(*it->second, *it->second->main);
  for (auto& shot : it->second->one_shots) {
    ma_sound_set_pitch(&shot->sound, std::max(pitch, 0.01f));
  }
}

void AudioSystem::SetLoop(SourceId id, bool loop) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  it->second->loop = loop;
  if (it->second->main) {
    ma_sound_set_looping(&it->second->main->sound, loop ? MA_TRUE : MA_FALSE);
  }
}

void AudioSystem::SetMute(SourceId id, bool mute) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  it->second->mute = mute;
  if (it->second->main) Apply(*it->second, *it->second->main);
}

void AudioSystem::SetSpatial(SourceId id, float spatial_blend, float min_distance,
                             float max_distance) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  Source& source = *it->second;
  source.spatial_blend = spatial_blend;
  source.min_distance = std::max(min_distance, 0.01f);
  source.max_distance = std::max(max_distance, source.min_distance);
  if (source.main) Apply(source, *source.main);
}

void AudioSystem::SetPosition(SourceId id, glm::vec3 position) {
  auto it = sources_.find(id);
  if (it == sources_.end()) return;
  Source& source = *it->second;
  source.position = position;
  if (source.spatial_blend <= 0.5f) return;
  if (source.main) ma_sound_set_position(&source.main->sound, position.x, position.y, position.z);
  for (auto& shot : source.one_shots) {
    ma_sound_set_position(&shot->sound, position.x, position.y, position.z);
  }
}

void AudioSystem::SetListener(glm::vec3 position, glm::vec3 forward, glm::vec3 up) {
  if (!engine_) return;
  ma_engine_listener_set_position(&engine_->engine, 0, position.x, position.y, position.z);
  ma_engine_listener_set_direction(&engine_->engine, 0, forward.x, forward.y, forward.z);
  ma_engine_listener_set_world_up(&engine_->engine, 0, up.x, up.y, up.z);
}

void AudioSystem::SetPaused(bool paused) {
  if (!engine_ || paused == paused_) return;
  paused_ = paused;
  if (paused) {
    ma_engine_stop(&engine_->engine);
  } else {
    ma_engine_start(&engine_->engine);
  }
}

void AudioSystem::SetMasterVolume(float volume) {
  if (engine_) ma_engine_set_volume(&engine_->engine, volume);
}

void AudioSystem::Update() {
  for (auto& [id, source] : sources_) {
    auto scale = source->one_shot_scales.begin();
    for (auto it = source->one_shots.begin(); it != source->one_shots.end();) {
      if (ma_sound_at_end(&(*it)->sound) || !ma_sound_is_playing(&(*it)->sound)) {
        ma_sound_uninit(&(*it)->sound);
        it = source->one_shots.erase(it);
        scale = source->one_shot_scales.erase(scale);
      } else {
        ++it;
        ++scale;
      }
    }
  }
}

float AudioSystem::GetClipLength(std::string_view res_path) {
  Sound* sound = NewSound(res_path);
  if (!sound) return 0.0f;
  float length = 0.0f;
  ma_sound_get_length_in_seconds(&sound->sound, &length);
  ma_sound_uninit(&sound->sound);
  delete sound;
  return length;
}

}  // namespace Mercury
