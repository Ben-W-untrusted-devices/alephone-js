/*
	Copyright (C) 2023 Benoit Hauquier and the "Aleph One" developers.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html
*/

#include "OpenALManager.h"
#include "Logging.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/emscripten.h>
// Web port (see ../../WEB_PORT_PLAN.md, M5): diagnostic + safety net for a
// still-reported "no sound" symptom after the non-loopback fallback below
// engages successfully (no crash, no error) -- routes through
// Module.printErr() rather than console.error() so it actually reaches the
// page's #log panel (this project's primary debugging surface, since
// Safari's dev tools have been unusable for this tab -- see game.html).
// `AL` here is Emscripten's own OpenAL port's internal namespace
// (emsdk/upstream/emscripten/src/lib/libopenal.js) -- accessible because
// all JS library code and EM_JS code share one compiled-output scope, not
// because it's an exported/public API; guarded with typeof/try-catch in
// case that ever stops holding. libopenal.js already arms a one-time
// resume-on-gesture listener when the context is created
// (autoResumeAudioContext, wired into alcCreateContext), but only once, at
// creation time -- if OpenALManager::Init() ever recreates the context
// (e.g. a preferences change, see Init()'s own Shutdown()-and-recreate
// path) after the page's last real user gesture, that one-time listener
// has nothing left to fire on. This logs the actual state so the next
// report is definitive rather than a guess, and arms one more redundant
// listener as a safety net for exactly that recreation case.
EM_JS(void, web_log_audio_context_state, (), {
    try {
        if (typeof AL === 'undefined' || !AL.currentCtx || !AL.currentCtx.audioCtx) {
            Module.printErr('[audio] no current AL context to inspect');
            return;
        }
        var ctx = AL.currentCtx.audioCtx;
        Module.printErr('[audio] AudioContext state: ' + ctx.state + ' (sampleRate=' + ctx.sampleRate + ')');
        var resumeOnce = function() {
            if (ctx.state === 'suspended') {
                ctx.resume().then(function() {
                    Module.printErr('[audio] AudioContext resumed, state now: ' + ctx.state);
                }).catch(function(e) {
                    Module.printErr('[audio] AudioContext resume() rejected: ' + e);
                });
            }
        };
        if (ctx.state === 'suspended') {
            for (var type of ['mousedown', 'keydown', 'touchstart']) {
                document.addEventListener(type, resumeOnce, { once: true });
            }
        }
        // Web port diagnostic (see ../../WEB_PORT_PLAN.md, M5): the
        // one-shot check/log above only sees the state at the moment this
        // runs (right after device/context creation) -- real-browser
        // reports of music silently dying mid-session (specifically right
        // when a dialog like Preferences or Continue Saved Game opens),
        // with no corresponding OpenALManager::Init()/SoundManager::SetStatus()
        // reinit log, rule out the device/context being recreated on the
        // C++ side. If the *browser* is independently suspending this same
        // AudioContext later for some reason, this is the only way to see
        // it: attach a real 'statechange' listener (not one-shot) that
        // logs every future transition, and try to resume again whenever
        // it goes back to suspended.
        if (!ctx.__a1StatechangeLogged) {
            ctx.__a1StatechangeLogged = true;
            ctx.addEventListener('statechange', function() {
                Module.printErr('[audio] AudioContext statechange -> ' + ctx.state);
                resumeOnce();
            });
        }
    } catch (e) {
        Module.printErr('[audio] web_log_audio_context_state failed: ' + e);
    }
});
#endif

bool OpenALManager::p_UsingLoopback = true;
LPALCLOOPBACKOPENDEVICESOFT OpenALManager::alcLoopbackOpenDeviceSOFT;
LPALCISRENDERFORMATSUPPORTEDSOFT OpenALManager::alcIsRenderFormatSupportedSOFT;
LPALCRENDERSAMPLESSOFT OpenALManager::alcRenderSamplesSOFT;
LPALGETSTRINGISOFT OpenALManager::alGetStringiSOFT;
LPALGENFILTERS OpenALManager::alGenFilters;
LPALDELETEFILTERS OpenALManager::alDeleteFilters;
LPALFILTERF OpenALManager::alFilterf;
LPALFILTERI OpenALManager::alFilteri;

OpenALManager* OpenALManager::instance = nullptr;

bool OpenALManager::Init(const AudioParameters& parameters) {

	if (instance) { //Don't bother recreating all the OpenAL context if nothing changed for it
		if (parameters.hrtf != instance->audio_parameters.hrtf || parameters.rate != instance->audio_parameters.rate
			|| parameters.channel_type != instance->audio_parameters.channel_type || parameters.sample_frame_size != instance->audio_parameters.sample_frame_size) {

#ifdef __EMSCRIPTEN__
			// Web port diagnostic (see ../../WEB_PORT_PLAN.md, M5): real-browser
			// reports of music/audio dying right when a dialog (Preferences,
			// Continue Saved Game) opens -- this is the one place OpenALManager
			// destroys and recreates its whole device/context (a fresh
			// AudioContext, needing its own gesture-triggered resume, and every
			// existing source/queue gone), so confirming whether -- and why --
			// this path is actually being hit is the fastest way to find out if
			// that's the cause. Safe to remove once root-caused.
			fprintf(stderr, "[audio] Init() reinitializing: hrtf %d->%d rate %u->%u channel_type %d->%d sample_frame_size %u->%u\n",
				instance->audio_parameters.hrtf, parameters.hrtf,
				instance->audio_parameters.rate, parameters.rate,
				(int)instance->audio_parameters.channel_type, (int)parameters.channel_type,
				instance->audio_parameters.sample_frame_size, parameters.sample_frame_size);
#endif
			Shutdown();

		} else {
			instance->UpdateParameters(parameters);
			return true;
		}
	} else {
		if (alcIsExtensionPresent(NULL, "ALC_SOFT_loopback")) {
#define LOAD_PROC(T, x)  ((x) = (T)alGetProcAddress(#x))
			LOAD_PROC(LPALCLOOPBACKOPENDEVICESOFT, alcLoopbackOpenDeviceSOFT);
			LOAD_PROC(LPALCISRENDERFORMATSUPPORTEDSOFT, alcIsRenderFormatSupportedSOFT);
			LOAD_PROC(LPALCRENDERSAMPLESSOFT, alcRenderSamplesSOFT);
			LOAD_PROC(LPALGETSTRINGISOFT, alGetStringiSOFT);
			LOAD_PROC(LPALGENFILTERS, alGenFilters);
			LOAD_PROC(LPALDELETEFILTERS, alDeleteFilters);
			LOAD_PROC(LPALFILTERI, alFilteri);
			LOAD_PROC(LPALFILTERF, alFilterf);
#undef LOAD_PROC
		} else {
			// Web port (see ../../WEB_PORT_PLAN.md, M5): expected on
			// Emscripten -- its built-in OpenAL port doesn't implement
			// ALC_SOFT_loopback (or AL_EXT_EFX, whose filter functions
			// would otherwise have been loaded in the block above; they
			// stay null, see GenerateEffects()/GetLowPassFilter()). Fall
			// back to a normal device instead of failing outright.
			logError("ALC_SOFT_loopback extension is not supported, falling back to a normal (non-loopback) device"); //Should never be the case natively, as long as >= OpenAL 1.14
			p_UsingLoopback = false;
		}
	}

	instance = new OpenALManager(parameters);
	bool success = instance->OpenDevice() && instance->LoadOptionalExtensions() && instance->GenerateSources() && instance->GenerateEffects();
#ifdef __EMSCRIPTEN__
	// Web port (see ../../WEB_PORT_PLAN.md, M5): OpenDevice() logs its own
	// success/AudioContext-state diagnostics -- this just confirms whether
	// the *whole* chain (LoadOptionalExtensions()/GenerateSources()/
	// GenerateEffects() too) actually succeeded, since a false here is
	// silently swallowed by SoundManager::SetStatus()'s caller.
	logError("OpenALManager::Init() overall result: %s", success ? "success" : "FAILED");
#endif
	return success;
}

bool OpenALManager::LoadOptionalExtensions() {

	for (const auto& extension : mapping_extensions_names)
		extension_support[extension.first] = alIsExtensionPresent(extension.second.c_str());

	return true;
}

void OpenALManager::ProcessAudioQueue() {

	std::shared_ptr<AudioPlayer> audioPlayer;
	while (audio_players_shared.pop(audioPlayer)) {
		audio_players_queue.push_back(audioPlayer);
	}

	UpdateListener();
	for (int i = 0; i < audio_players_queue.size(); i++) {

		auto audio = audio_players_queue.front();

#ifdef __EMSCRIPTEN__
		// Web port diagnostic (see ../../WEB_PORT_PLAN.md, M5): real-browser
		// testing shows players genuinely get queued (queue_size briefly
		// non-zero) then disappear again almost immediately -- meaning
		// something in this exact chain returns false right away. Evaluated
		// step by step (still short-circuiting exactly like the single
		// expression below, so AssignSource()/Update()/Play() are never
		// called out of order or after an earlier failure) so the log says
		// which stage actually failed instead of just "didn't play". Safe
		// to remove once root-caused.
		bool stepAssigned = false, stepUpdated = false, stepPlayed = false;
		bool mustStillPlay = false;
		if (!audio->stop_signal) {
			stepAssigned = audio->AssignSource();
			if (stepAssigned) {
				stepUpdated = audio->Update();
				if (stepUpdated) {
					stepPlayed = audio->Play();
					mustStillPlay = stepPlayed;
				}
			}
		}
		if (!mustStillPlay && !audio->stop_signal) {
			fprintf(stderr, "[audio player] failed: assigned=%d updated=%d played=%d\n",
				stepAssigned, stepUpdated, stepPlayed);
		}
#else
		const bool mustStillPlay = !audio->stop_signal && audio->AssignSource() && audio->Update() && audio->Play();
#endif

		audio_players_queue.pop_front();

		if (!mustStillPlay) {
			RetrieveSource(audio);
			continue;
		}

		audio_players_queue.push_back(audio); //We have just processed a part of the data for you, now wait your next turn
	}
}

//we update our listener's position for 3D sounds
void OpenALManager::UpdateListener() {

	if (!audio_parameters.sounds_3d) return;

	const auto& listener = listener_location.Get();

	const auto yaw = listener.yaw * angleConvert;
	const auto pitch = listener.pitch * angleConvert;

	ALfloat u = std::cos(degreToRadian * yaw) * std::cos(degreToRadian * pitch);
	ALfloat	v = std::sin(degreToRadian * yaw) * std::cos(degreToRadian * pitch);
	ALfloat	w = std::sin(degreToRadian * pitch);

	//OpenAL uses the same coordinate system as OpenGL, so we have to swap Z <-> Y
	ALfloat vectorDirection[] = { u, w, v, 0, 1, 0 };

	ALfloat position[] = { (float)(listener.point.x) / WORLD_ONE,
						   (float)(listener.point.z) / WORLD_ONE,
						   (float)(listener.point.y) / WORLD_ONE };

	ALfloat velocity[] = { (float)listener.velocity.i / WORLD_ONE,
						   (float)listener.velocity.k / WORLD_ONE,
						   (float)listener.velocity.j / WORLD_ONE };

#ifdef __EMSCRIPTEN__
	// Web port diagnostic (see ../../WEB_PORT_PLAN.md, M5): real-browser
	// reports of a hard crash ("Out of bounds memory access") specifically
	// on entering real gameplay (Begin New Game / Continue Saved Game) --
	// listener position/orientation is real 3D-positional AL state that
	// was never exercised while only menu UI sounds worked. Only logs if
	// something is actually non-finite, so this stays silent in normal
	// operation. Safe to remove once root-caused.
	auto allFinite = [](const ALfloat* v, int n) {
		for (int i = 0; i < n; i++) if (!std::isfinite(v[i])) return false;
		return true;
	};
	if (!allFinite(vectorDirection, 6) || !allFinite(position, 3) || !allFinite(velocity, 3)) {
		fprintf(stderr, "[audio] UpdateListener non-finite value! pos=(%f,%f,%f) vel=(%f,%f,%f) dir=(%f,%f,%f,%f,%f,%f)\n",
			position[0], position[1], position[2], velocity[0], velocity[1], velocity[2],
			vectorDirection[0], vectorDirection[1], vectorDirection[2], vectorDirection[3], vectorDirection[4], vectorDirection[5]);
	}
#endif

	alListenerfv(AL_ORIENTATION, vectorDirection);
	alListenerfv(AL_POSITION, position);
	alListenerfv(AL_VELOCITY, velocity);
}

void OpenALManager::SetMasterVolume(float volume) {
	volume = std::min(1.f, std::max(volume, 0.f));
	if (master_volume == volume) return;
	master_volume = volume;
	ResyncPlayers();
}

void OpenALManager::SetMusicVolume(float volume) {
	volume = std::min(10.f, std::max(volume, 0.f));
	if (music_volume == volume) return;
	music_volume = volume;
	ResyncPlayers(true);
}

void OpenALManager::ResyncPlayers(bool music_players_only) {
	SDL_LockAudio();

	for (auto& player : audio_players_queue) 
	{
		if (!music_players_only || std::dynamic_pointer_cast<MusicPlayer>(player) != nullptr)
		{
			player->is_sync_with_al_parameters = false;
		}
	}

	SDL_UnlockAudio();
}

void OpenALManager::Start() {
	// Web port (see ../../WEB_PORT_PLAN.md, M5): no SDL audio device exists
	// in non-loopback mode (see the constructor) -- process_audio_active is
	// managed directly instead of read back from SDL_GetAudioStatus(), and
	// Tick() (not a MixerCallback SDL silences/resumes) is what actually
	// gates ProcessAudioQueue() on it.
	if (!p_UsingLoopback) {
		process_audio_active = !is_using_recording_device;
		return;
	}

	SDL_PauseAudio(is_using_recording_device); //Start playing only if not recording playback
	process_audio_active = SDL_GetAudioStatus() != SDL_AUDIO_STOPPED;
}

void OpenALManager::Pause(bool paused) {
	if (!process_audio_active || paused_audio == paused) return;

	paused_audio = paused;
	if (p_UsingLoopback) SDL_PauseAudio(paused_audio);
	elapsed_pause_time = machine_tick_count() - elapsed_pause_time;
}

void OpenALManager::Stop() {
	if (p_UsingLoopback) SDL_PauseAudio(true);
	StopAllPlayers();
	process_audio_active = false;
}

void OpenALManager::ToggleDeviceMode(bool recording_device) {
	is_using_recording_device = recording_device;
	if (p_UsingLoopback) SDL_PauseAudio(is_using_recording_device);
}

void OpenALManager::Tick() {
	// Web port (see ../../WEB_PORT_PLAN.md, M5): the non-loopback
	// counterpart to MixerCallback()/GetPlayBackAudio() -- there's no SDL
	// audio callback to drive ProcessAudioQueue() in this mode, so shell.cpp's
	// per-frame tick calls this directly instead. No alcRenderSamplesSOFT
	// call needed afterward: OpenAL renders/outputs audio itself once
	// sources are queued and playing, it isn't pulled into a buffer we own.
	if (p_UsingLoopback || !process_audio_active || paused_audio) return;
#ifdef __EMSCRIPTEN__
	// Web port diagnostic (see ../../WEB_PORT_PLAN.md, M5): AudioContext is
	// confirmed running now, but real-browser testing still reports total
	// silence -- this narrows down whether the game is even attempting to
	// queue anything (empty queue -> look upstream, at SoundManager/Music)
	// versus queuing sources that just never produce audible output (non-
	// empty queue -> look at AudioPlayer/OpenAL state itself). Throttled to
	// roughly once every 3 seconds to stay readable in the #log panel.
	// Safe to remove once root-caused.
	static uint32 last_tick_log = 0;
	uint32 now = machine_tick_count();
	if (now - last_tick_log >= 3 * MACHINE_TICKS_PER_SECOND) {
		last_tick_log = now;
		fprintf(stderr, "[audio tick] queue_size=%zu master_volume=%.3f music_volume=%.3f\n",
			audio_players_queue.size(), master_volume.load(), music_volume.load());
	}
#endif
	ProcessAudioQueue();
}

std::shared_ptr<SoundPlayer> OpenALManager::PlaySound(const Sound& sound, const SoundParameters& parameters) {
	if (!process_audio_active) return std::shared_ptr<SoundPlayer>();
	auto soundPlayer = std::make_shared<SoundPlayer>(sound, parameters);
	audio_players_shared.push(soundPlayer);
	return soundPlayer;
}

std::shared_ptr<MusicPlayer> OpenALManager::PlayMusic(std::vector<MusicPlayer::Sequence>& sequences, uint32_t starting_sequence_index, uint32_t starting_segment_index, const MusicParameters& parameters) {
	if (!process_audio_active) return std::shared_ptr<MusicPlayer>();
	auto musicPlayer = std::make_shared<MusicPlayer>(sequences, starting_sequence_index, starting_segment_index, parameters);
	audio_players_shared.push(musicPlayer);
	return musicPlayer;
}

//Used for video playback
std::shared_ptr<StreamPlayer> OpenALManager::PlayStream(CallBackStreamPlayer callback, uint32_t rate, bool stereo, AudioFormat audioFormat, void* userdata) {
	if (!process_audio_active) return std::shared_ptr<StreamPlayer>();
	auto streamPlayer = std::make_shared<StreamPlayer>(callback, rate, stereo, audioFormat, userdata);
	audio_players_shared.push(streamPlayer);
	return streamPlayer;
}

//It's not a good idea generating dynamically a new source for each player
//It's slow so it's better having a pool, also we already know the max amount
//of supported simultaneous playing sources for the device
std::unique_ptr<AudioPlayer::AudioSource> OpenALManager::PickAvailableSource(const AudioPlayer& audioPlayer) {
	if (sources_pool.empty()) {
		const auto& victimPlayer = *std::min_element(audio_players_queue.begin(), audio_players_queue.end(),
			[](const std::shared_ptr<AudioPlayer>& a, const std::shared_ptr<AudioPlayer>& b)
			{  return a->audio_source && a->GetPriority() < b->GetPriority(); });

		return victimPlayer->GetPriority() < audioPlayer.GetPriority() ? victimPlayer->RetrieveSource() : nullptr;
	}

	auto source = std::move(sources_pool.front());
	sources_pool.pop();
	return source;
}

void OpenALManager::StopAllPlayers() {
	SDL_LockAudio();

	for (auto& audioPlayer : audio_players_queue) {
		RetrieveSource(audioPlayer);
	}

	audio_players_queue.clear();

	std::shared_ptr<AudioPlayer> audioPlayer;
	while (audio_players_shared.pop(audioPlayer)) {
		audioPlayer->is_active = false;
	}

	SDL_UnlockAudio();
}

void OpenALManager::RetrieveSource(const std::shared_ptr<AudioPlayer>& player) {
	auto audioSource = player->RetrieveSource();
	if (audioSource) sources_pool.push(std::move(audioSource));
	player->is_active = false;
}

//this is used with the recording device and this allows OpenAL to
//not output the audio once it has mixed it but instead, makes 
//the mixed data available with alcRenderSamplesSOFT
void OpenALManager::GetPlayBackAudio(uint8* data, int length) {
	ProcessAudioQueue();
	alcRenderSamplesSOFT(p_ALCDevice, data, length);
}

OpenALManager::HrtfSupport OpenALManager::GetHrtfSupport() const {
	ALCint hrtfStatus;
	alcGetIntegerv(p_ALCDevice, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus);

	switch (hrtfStatus) {
		case ALC_HRTF_DENIED_SOFT:
		case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT:
			return HrtfSupport::Unsupported;
		case ALC_HRTF_REQUIRED_SOFT:
			return HrtfSupport::Required;
		default:
			return HrtfSupport::Supported;
	}
}

bool OpenALManager::IsHrtfEnabled() const {
	ALCint hrtfStatus;
	alcGetIntegerv(p_ALCDevice, ALC_HRTF_SOFT, 1, &hrtfStatus);
	return hrtfStatus;
}

bool OpenALManager::OpenDevice() {
	if (p_ALCDevice) return true;

	// Web port (see ../../WEB_PORT_PLAN.md, M5): no loopback extension under
	// Emscripten (see Init()) -- open a normal device instead. OpenAL then
	// owns real-time output itself (via Emscripten's Web-Audio-backed OpenAL
	// port), driven by Tick() rather than pulled through MixerCallback/
	// alcRenderSamplesSOFT. A real device picks its own native format/rate,
	// so none of the ALC_FORMAT_*/ALC_FREQUENCY loopback-only attributes
	// below apply -- only ALC_HRTF_SOFT still makes sense to request.
	if (!p_UsingLoopback) {
		p_ALCDevice = alcOpenDevice(nullptr);
		if (!p_ALCDevice) {
			logError("Could not open audio device");
			return false;
		}

		ALCint attrs[] = {
			ALC_HRTF_SOFT, audio_parameters.hrtf,
			0,
		};

		p_ALCContext = alcCreateContext(p_ALCDevice, attrs);
		if (!p_ALCContext) {
			logError("Could not create audio context");
			return false;
		}

		if (!alcMakeContextCurrent(p_ALCContext)) {
			logError("Could not make audio context current");
			return false;
		}

		web_log_audio_context_state();
		return true;
	}

	p_ALCDevice = alcLoopbackOpenDeviceSOFT(nullptr);
	if (!p_ALCDevice) {
		logError("Could not open audio loopback device");
		return false;
	}

	if (!openal_rendering_format) {
		openal_rendering_format = GetBestOpenALSupportedFormat();
	}

	if (openal_rendering_format) {
		ALCint attrs[] = {
			ALC_FORMAT_TYPE_SOFT,     openal_rendering_format,
			ALC_FORMAT_CHANNELS_SOFT, mapping_sdl_openal_channel.at(audio_parameters.channel_type),
			ALC_FREQUENCY,            static_cast<ALCint>(audio_parameters.rate),
			ALC_HRTF_SOFT,            audio_parameters.hrtf,
			0,
		};

		p_ALCContext = alcCreateContext(p_ALCDevice, attrs);
		if (!p_ALCContext) {
			logError("Could not create audio context from loopback device");
			return false;
		}

		if (!alcMakeContextCurrent(p_ALCContext)) {
			logError("Could not make audio context from loopback device current");
			return false;
		}

		return true;
	}

	return false;
}

bool OpenALManager::CloseDevice() {
	if (!alcMakeContextCurrent(nullptr)) {
		logError("Could not remove current audio context");
		return false;
	}

	if (p_ALCContext) {
		alcDestroyContext(p_ALCContext);
		p_ALCContext = nullptr;
	}

	if (p_ALCDevice) {
		if (!alcCloseDevice(p_ALCDevice)) {
			logError("Could not close audio device");
			return false;
		}

		p_ALCDevice = nullptr;
	}

	return true;
}

bool OpenALManager::GenerateEffects() {
	// Web port (see ../../WEB_PORT_PLAN.md, M5): AL_EXT_EFX isn't implemented
	// under Emscripten (see Init()) -- alGenFilters etc. stay null in that
	// case. Skip filter creation entirely; GetLowPassFilter() below already
	// tolerates this (returns AL_FILTER_NULL), degrading to "no obstruction
	// muffling" rather than crashing on a null function pointer.
	if (!alGenFilters) {
		low_pass_filter = AL_FILTER_NULL;
		return true;
	}

	alGenFilters(1, &low_pass_filter);
	alFilteri(low_pass_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
	alFilterf(low_pass_filter, AL_LOWPASS_GAIN, 1.f);
	alFilterf(low_pass_filter, AL_LOWPASS_GAINHF, 1.f);
	return alGetError() == AL_NO_ERROR;
}

ALuint OpenALManager::GetLowPassFilter(float highFrequencyGain) const {
	if (!alFilterf) return AL_FILTER_NULL;
	alFilterf(low_pass_filter, AL_LOWPASS_GAINHF, highFrequencyGain);
	return low_pass_filter;
}

bool OpenALManager::GenerateSources() {

	/* how many simultaneous sources are supported on this device ? */
	int monoSources, stereoSources;
	alcGetIntegerv(p_ALCDevice, ALC_MONO_SOURCES, 1, &monoSources);
	alcGetIntegerv(p_ALCDevice, ALC_STEREO_SOURCES, 1, &stereoSources);

	// Web port (see ../../WEB_PORT_PLAN.md, M5): Emscripten's OpenAL port
	// reports both of these as "effectively unlimited" (INT32_MAX each --
	// Web Audio has no hardware source-count limit to report). Summing them
	// unclamped overflows a signed int (UB, wraps negative in practice),
	// which then became a huge size_t once passed to std::vector's
	// constructor below -- crashing with an uncaught length_error ("Unhandled
	// exception: vector") before any audio device finished initializing.
	// Clamp each to a sane cap first; this game never needs anywhere near
	// this many simultaneous sources regardless of what the device reports,
	// and each one becomes a real Web Audio node graph under Emscripten
	// (GenerateSources() pre-allocates the whole pool up front), so keeping
	// this modest avoids needlessly creating hundreds of them in a browser
	// tab for a device that has no real capacity limit to report.
	constexpr int kMaxSourcesPerType = 64;
	monoSources = std::min(monoSources, kMaxSourcesPerType);
	stereoSources = std::min(stereoSources, kMaxSourcesPerType);
	int nbSources = monoSources + stereoSources;

	std::vector<ALuint> sources_id(nbSources);
	alGenSources(nbSources, sources_id.data());
	for (auto source_id : sources_id) {

		alSourcei(source_id, AL_BUFFER, 0);
		alSourceRewind(source_id);

		if (alGetError() != AL_NO_ERROR) {
			logError("Could not set source parameters: [source id: %d] [number of sources: %d]", source_id, nbSources);
			return false;
		}

		AudioPlayer::AudioSource audioSource;
		audioSource.source_id = source_id;
		ALuint buffers_id[num_buffers];
		alGenBuffers(num_buffers, buffers_id);
		if (alGetError() != AL_NO_ERROR) {
			logError("Could not create source buffers: [source id: %d] [number of sources: %d]", source_id, nbSources);
			return false;
		}

		for (int i = 0; i < num_buffers; i++) {
			audioSource.buffers.insert({ buffers_id[i], false });
		}

		sources_pool.push(std::make_unique<AudioPlayer::AudioSource>(audioSource));
	}

	return !sources_id.empty();
}

OpenALManager::OpenALManager(const AudioParameters& parameters) {
	UpdateParameters(parameters);
	alListener3i(AL_POSITION, 0, 0, 0);

	// Web port (see ../../WEB_PORT_PLAN.md, M5): without a loopback device
	// there's no PCM buffer for SDL to pull from, and GetBestOpenALSupportedFormat()
	// would otherwise fall back to calling alcLoopbackOpenDeviceSOFT directly
	// (see its own definition below) -- a null function pointer in this mode.
	// OpenAL drives real output itself once OpenDevice() runs; audio_parameters
	// (already set by UpdateParameters() above) is all that's needed.
	if (!p_UsingLoopback) return;

	auto openalFormat = GetBestOpenALSupportedFormat();
	assert(openalFormat && "Audio format not found or not supported");
	SDL_AudioSpec desired = {};
	desired.freq = parameters.rate;
	desired.format = mapping_openal_sdl_format.at(openalFormat);
	desired.channels = static_cast<int>(parameters.channel_type);
	desired.samples = parameters.sample_frame_size;
	desired.callback = MixerCallback;
	desired.userdata = reinterpret_cast<void*>(this);

	if (SDL_OpenAudio(&desired, &sdl_audio_specs_obtained) < 0) {
		CleanEverything();
	} else {
		audio_parameters.rate = sdl_audio_specs_obtained.freq;
		audio_parameters.channel_type = static_cast<ChannelType>(sdl_audio_specs_obtained.channels);
		openal_rendering_format = mapping_sdl_openal_format.at(sdl_audio_specs_obtained.format);
	}
}

void OpenALManager::MixerCallback(void* usr, uint8* stream, int len) {
	auto manager = (OpenALManager*)usr;
	int frameSize = manager->sdl_audio_specs_obtained.channels * SDL_AUDIO_BITSIZE(manager->sdl_audio_specs_obtained.format) / 8;
	manager->GetPlayBackAudio(stream, len / frameSize);
}

void OpenALManager::CleanEverything() {
	Stop();

	while (!sources_pool.empty()) {
		const auto& audioSource = sources_pool.front();
		alDeleteSources(1, &audioSource->source_id);

		for (auto const& buffer : audioSource->buffers) {
			alDeleteBuffers(1, &buffer.first);
		}

		sources_pool.pop();
	}

	// Web port (see ../../WEB_PORT_PLAN.md, M5): alDeleteFilters is only
	// loaded when AL_EXT_EFX is available (see Init()) -- null otherwise.
	if (alDeleteFilters) alDeleteFilters(1, &low_pass_filter);
	bool closedDevice = CloseDevice();
	assert(closedDevice && "Could not close audio device");
}

int OpenALManager::GetBestOpenALSupportedFormat() {
	auto device = p_ALCDevice ? p_ALCDevice : alcLoopbackOpenDeviceSOFT(nullptr);
	if (!device) {
		logError("Could not open audio loopback device to find best rendering format");
		return 0;
	}

	ALCint format = 0;
	for (int i = 0; i < format_type.size(); i++) {

		ALCint attrs[] = {
			ALC_FORMAT_TYPE_SOFT,     format_type[i],
			ALC_FORMAT_CHANNELS_SOFT, mapping_sdl_openal_channel.at(audio_parameters.channel_type),
			ALC_FREQUENCY,            static_cast<ALCint>(audio_parameters.rate)
		};

		if (alcIsRenderFormatSupportedSOFT(device, attrs[5], attrs[3], attrs[1]) == AL_TRUE) {
			format = format_type[i];
			break;
		}
	}

	if (!p_ALCDevice) {
		if (!alcCloseDevice(device)) {
			logError("Could not close audio loopback device to find best rendering format");
			return 0;
		}
	}

	return format;
}

void OpenALManager::UpdateParameters(const AudioParameters& parameters) {
	audio_parameters = parameters;
	SetMasterVolume(parameters.master_volume);
	SetMusicVolume(parameters.music_volume);
}

void OpenALManager::Shutdown() {
	delete instance;
	instance = nullptr;
}

OpenALManager::~OpenALManager() {
	CleanEverything();
	SDL_CloseAudio();
}
