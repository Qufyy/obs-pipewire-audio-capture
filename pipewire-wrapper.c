/*
Copyright (C) 2021 by Dimitris Papaioannou <jimpap31@outlook.com.gr>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
//Based on the linux-pulseaudio plugin design by Leonhard Oelke

#include "pipewire-wrapper.h"

#include <pipewire/pipewire.h>
#include <pipewire/global.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/type-info.h>

static uint32_t pipewire_refs = 0;
static struct pw_thread_loop *pipewire_mainloop = NULL;
static struct pw_context *pipewire_context = NULL;
static struct pw_core *pipewire_core = NULL;
static struct pw_registry *pipewire_registry = NULL;

void pipewire_init()
{
	if (pipewire_refs == 0) {
		pw_init(NULL, NULL);

		pipewire_mainloop = pw_thread_loop_new("OBS Studio", NULL);

		pipewire_lock();

		if (pw_thread_loop_start(pipewire_mainloop) != 0) {
			pw_thread_loop_destroy(pipewire_mainloop);
			pipewire_unlock();
			return;
		};

		pipewire_context = pw_context_new(
			pw_thread_loop_get_loop(pipewire_mainloop), NULL, 0);

		pipewire_core = pw_context_connect(pipewire_context, NULL, 0);

		pipewire_registry = pw_core_get_registry(
			pipewire_core, PW_VERSION_REGISTRY, 0);

		pipewire_unlock();
	}
	pipewire_refs++;
}

void pipewire_unref()
{
	if (--pipewire_refs == 0) {
		pipewire_lock();

		if (pipewire_registry) {
			pw_proxy_destroy((struct pw_proxy *)pipewire_registry);
		}
		if (pipewire_core) {
			pw_core_disconnect(pipewire_core);
		}

		if (pipewire_context) {
			pw_context_destroy(pipewire_context);
		}

		pipewire_unlock();

		if (pipewire_mainloop) {
			pw_thread_loop_stop(pipewire_mainloop);
			pw_thread_loop_destroy(pipewire_mainloop);
		}

		pw_deinit();
	}
}

void pipewire_lock()
{
	pw_thread_loop_lock(pipewire_mainloop);
}

void pipewire_unlock()
{
	pw_thread_loop_unlock(pipewire_mainloop);
}

void pipewire_wait()
{
	pw_thread_loop_wait(pipewire_mainloop);
}

void pipewire_continue()
{
	pw_thread_loop_signal(pipewire_mainloop, false);
}

void pipewire_add_registry_listener(bool call_now, struct spa_hook *hook,
				    const struct pw_registry_events *callbacks,
				    void *data)
{
	pipewire_lock();

	pipewire_registry =
		pw_core_get_registry(pipewire_core, PW_VERSION_REGISTRY, 0);
	pw_registry_add_listener(pipewire_registry, hook, callbacks, data);

	if (call_now)
		pipewire_wait();
	pipewire_unlock();
}

struct pw_stream *pipewire_stream_new(bool capture_sink,
				      struct spa_hook *stream_listener,
				      const struct pw_stream_events *callbacks,
				      void *data)
{
	char *capture_sink_str = "false";
	if (capture_sink)
		capture_sink_str = "true";

	struct pw_properties *props = pw_properties_new(
		PW_KEY_APP_NAME, "OBS Studio", PW_KEY_APP_ICON_NAME, "obs",
		PW_KEY_NODE_NAME, "OBS Studio", PW_KEY_MEDIA_ROLE, "Production",
		PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
		PW_KEY_STREAM_CAPTURE_SINK, capture_sink_str, NULL);

	pipewire_lock();

	struct pw_stream *stream =
		pw_stream_new(pipewire_core, "OBS Studio", props);
	pw_stream_add_listener(stream, stream_listener, callbacks, data);

	pipewire_unlock();
	return stream;
}

int pipewire_stream_connect(struct pw_stream *stream,
			    const struct spa_pod **params, uint32_t node_id)
{
	pipewire_lock();

	int res = -1;
	res = pw_stream_connect(stream, PW_DIRECTION_INPUT, node_id,
				PW_STREAM_FLAG_AUTOCONNECT |
					PW_STREAM_FLAG_MAP_BUFFERS,
				params, 1);

	pipewire_unlock();

	return res;
}

int pipewire_stream_disconnect(struct pw_stream *stream)
{
	pipewire_lock();

	int res = -1;
	res = pw_stream_disconnect(stream);

	pipewire_unlock();

	return res;
}

void pipewire_stream_destroy(struct pw_stream *stream)
{
	pipewire_lock();

	pw_stream_disconnect(stream);
	pw_stream_destroy(stream);

	pipewire_unlock();
}