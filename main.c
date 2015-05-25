#include <math.h>
#include <stdio.h>
#include <unistd.h>

#include <jack/jack.h>
#include <jack/midiport.h>


/******************
 * User-specified *
 ******************/

typedef struct {
	char channel;
	char instrument;
	char note;
	int playing;
} midi_event_t;

midi_event_t kick_drum = {
	0x0A, // Channel 10: Drums
	0x00, // Instrument 0: Grand Piano (Channel 10 ignores instrument)
	0x24, // Note 36: Bass drum
	0x00, // Not playing
};

midi_event_t midi_event;

int analog = 0; // Respond to velocity?
float threshold = .25; // Minimal volume to trigger the MIDI note
int min_time_between_hits = 1024;

/************
 * Internal *
 ************/

jack_client_t *client;

jack_port_t *input_port;
jack_port_t *output_port;

/*************
 * Callbacks *
 *************/

void toggle_midi_event(void *out, int frame, midi_event_t *midi_event, float volume) {
	unsigned char* buffer = jack_midi_event_reserve(out, frame, 3);
	buffer[0] = 0x80 | ((midi_event->playing = !midi_event->playing) << 4) | (midi_event->channel & 0xF); // On this channel, set on or off this
	buffer[1] = midi_event->note; // note,
	buffer[2] = 0x7F * (analog ? volume : 1); // played at this velocity.
}

int process(jack_nframes_t nframes, void *arg) {
	jack_default_audio_sample_t *in = (jack_default_audio_sample_t *)jack_port_get_buffer(input_port, nframes);
	void *out = jack_port_get_buffer(output_port, nframes);

	jack_midi_clear_buffer(out);

	static int count = -1;

	int is_on;
	float volume;

	int frame;
	for(frame = 0; frame < nframes; frame++) {
		if((is_on = (volume = fabs(in[frame])) > threshold))
			count = 0;
		else if(count++ == min_time_between_hits)
			count = -1;

		if(is_on != midi_event.playing && -midi_event.playing == count)
			toggle_midi_event(out, frame, &midi_event, volume);
	}

	return 0;
}

int sample_rate(jack_nframes_t nframes, void *arg) {
	printf("the sample rate is now %luHz\n", (long unsigned int)nframes);
	return 0;
}

void error(const char *desc) {
	fprintf(stderr, "JACK error: %s\n", desc);
}

void jack_shutdown(void *arg) {
	fprintf(stderr, "JACK error: jack daemon closed\n");
	exit(1);
}

/*******************
 * Other functions *
 *******************/

void jack_stop() {
	jack_client_close(client);
}

void jack_start() {
	jack_set_error_function(error);

	client = jack_client_open("MicToMIDI", JackNullOption, NULL);
	if (client == 0) {
		fprintf(stderr, "jack server not running?\n");
		exit(1);
	}

	jack_set_process_callback(client, process, 0);
	jack_set_sample_rate_callback(client, sample_rate, 0);
	jack_on_shutdown(client, jack_shutdown, 0);

	input_port = jack_port_register(client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	output_port = jack_port_register(client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if(jack_activate(client)) {
		fprintf(stderr, "cannot activate client");
		exit(1);
	}
	atexit(jack_stop);

	const char **ports;
	if((ports = jack_get_ports(client, NULL, NULL, JackPortIsPhysical | JackPortIsOutput)) == NULL)
	{
		fprintf(stderr, "cannot find any physical capture ports\n");
		exit(1);
	}
	if(jack_connect(client, ports[0], jack_port_name(input_port)))
		fprintf(stderr, "cannot connect to physical capture port\n");

	jack_free(ports);
	if((ports = jack_get_ports(client, NULL, "midi", JackPortIsInput)) == NULL)
	{
		fprintf(stderr, "cannot find any MIDI playback ports\n");
		exit(1);
	}
	if(jack_connect(client, jack_port_name(output_port), ports[0]))
		fprintf(stderr, "cannot connect to MIDI playback port\n");
}

int main(int argc, char *argv[])
{
	midi_event = kick_drum;

	char option;
	while((option = getopt(argc, argv, "at:m:c:i:n:")) != -1)
		switch(option) {
			case 'a':
				analog = 1;
				break;
			case 't':
				threshold = atof(optarg);
				break;
			case 'm':
				min_time_between_hits = atoi(optarg);
				break;
			case 'c':
				midi_event.channel = atoi(optarg);
				break;
			case 'i':
				midi_event.instrument = atoi(optarg);
				break;
			case 'n':
				midi_event.note = atoi(optarg);
				break;

			case '?':
				exit(1);
			default:
				printf("What's going on here?\n");
				break;
		}

	jack_start();

	while(1)
		sleep(1);

	return 0;
}
