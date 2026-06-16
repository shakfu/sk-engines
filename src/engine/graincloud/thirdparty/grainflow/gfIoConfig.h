#pragma once
namespace Grainflow
{
	template<typename T = double>
	struct gf_io_config
	{
		//Outputs
		T** grain_output = nullptr;
		T** grain_state = nullptr;
		T** grain_progress = nullptr;
		T** grain_playhead = nullptr;
		T** grain_amp = nullptr;
		T** grain_envelope = nullptr;
		T** grain_buffer_channel = nullptr;
		T** grain_stream_channel = nullptr;

		//Inputs
		T** grain_clock = nullptr;
		T** traversal_phasor = nullptr;
		T** fm = nullptr;
		T** am = nullptr;

		int grain_clock_chans;
		int traversal_phasor_chans;
		int fm_chans;
		int am_chans;

		bool livemode = false;
		int block_size = 0;
		int samplerate = 1;
	};
}
