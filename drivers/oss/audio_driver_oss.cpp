/*************************************************************************/
/*  audio_driver_oss.cpp                                                 */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2018 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2018 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/
#include "audio_driver_oss.h"

#ifdef OSS_ENABLED

#include "os/os.h"
#include "project_settings.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/soundcard.h>

#if defined(__FreeBSD__)
#define SND_DEVICE "/dev/dsp"
#else
/* linux or windows uses this? */
#define SND_DEVICE "/dev/mixer"
#endif

#define SAMPLE_FMT AFMT_S16_NE
#define SAMPLE_VARIATION 500 /* allowable variation of the requested sample rate */

Error AudioDriverOSS::init()
{
	int tmp;

	active = false;
	thread_exited = false;
	exit_thread = false;
	samples_in = NULL;

	mix_rate = DEFAULT_MIX_RATE;
	speaker_mode = SPEAKER_MODE_STEREO;
	channels = 2;

	int latency = GLOBAL_DEF("audio/output_latency", DEFAULT_OUTPUT_LATENCY);
	buffer_frames = closest_power_of_2(latency * mix_rate / 1000);

	samples_in = memnew_arr(int32_t, buffer_frames * channels);

	// open device
	snd_dev_id = open(SND_DEVICE, O_WRONLY, 0);
	if (snd_dev_id == -1) {
		fprintf(stderr, "OSS Audio ERR: %i\n", errno);
		ERR_FAIL_COND_V(snd_dev_id == -1, ERR_CANT_OPEN);
		return ERR_CANT_OPEN;
	}

	// set sample format
	tmp = SAMPLE_FMT;
	if (ioctl (snd_dev_id, SNDCTL_DSP_SETFMT, &tmp) == -1) {
		fprintf(stderr, "OSS Audio error setting sample format: %i\n", errno);
		ERR_FAIL_COND_V(0, ERR_INVALID_PARAMETER);
	}
	if (tmp != SAMPLE_FMT) {
		fprintf(stderr, "OSS Audio: %i is a bad sample format.\n", SAMPLE_FMT);
		ERR_FAIL_COND_V(tmp!=SAMPLE_FMT, ERR_INVALID_PARAMETER);
	}

	// set channels
	tmp = channels;
	if (ioctl (snd_dev_id, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
		fprintf(stderr, "OSS Audio: unable to get requested channels.\n");
		ERR_FAIL_COND_V(0, ERR_INVALID_PARAMETER);
	}
	if (tmp != channels) {
		fprintf(stderr, "OSS Audio: got %i channels instead of %i.\n", tmp, channels);
		ERR_FAIL_COND_V(tmp!=channels, ERR_INVALID_PARAMETER);
	}

	// set sample rate
	unsigned int utmp = mix_rate;
	if (ioctl (snd_dev_id, SNDCTL_DSP_SPEED, &utmp) == -1) {
		fprintf(stderr, "OSS Audio: unable to set the sample rate.\n");
		ERR_FAIL_COND_V(0, ERR_INVALID_PARAMETER);
	}
	if ( (utmp - mix_rate) > SAMPLE_VARIATION) {
		fprintf(stderr, "OSS Audio: got sample rate of %u instead of %u.\n", utmp, mix_rate);
		ERR_FAIL_COND_V( (utmp - mix_rate) > SAMPLE_VARIATION, ERR_INVALID_PARAMETER);
	}

	mutex = Mutex::create();
	thread = Thread::create(AudioDriverOSS::thread_func, this);

	return OK;
};

void AudioDriverOSS::thread_func(void *p_udata) {

	AudioDriverOSS *ad = (AudioDriverOSS *)p_udata;

	uint64_t usdelay = (ad->buffer_frames / float(ad->mix_rate)) * 1000000;

	while (!ad->exit_thread) {

		if (ad->active) {

			ad->lock();

			ad->audio_server_process(ad->buffer_frames, ad->samples_in);

			ad->unlock();
		};

		OS::get_singleton()->delay_usec(usdelay);
	};

	ad->thread_exited = true;
};

void AudioDriverOSS::start() {

	active = true;
};

int AudioDriverOSS::get_mix_rate() const {

	return mix_rate;
};

AudioDriver::SpeakerMode AudioDriverOSS::get_speaker_mode() const {

	return speaker_mode;
};

void AudioDriverOSS::lock() {

	if (!thread || !mutex)
		return;
	mutex->lock();
};

void AudioDriverOSS::unlock() {

	if (!thread || !mutex)
		return;
	mutex->unlock();
};

void AudioDriverOSS::finish() {

	if (!thread)
		return;

	exit_thread = true;
	Thread::wait_to_finish(thread);

	if (samples_in) {
		memdelete_arr(samples_in);
	};

	memdelete(thread);
	if (mutex)
		memdelete(mutex);
	thread = NULL;
};

AudioDriverOSS::AudioDriverOSS() {

	mutex = NULL;
	thread = NULL;
};

AudioDriverOSS::~AudioDriverOSS(){

};

#endif
