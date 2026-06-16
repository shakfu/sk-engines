#pragma once
#include "gfIoConfig.h"
#include "gfParam.h"

namespace Grainflow
{
	struct gf_buffer_info
	{
	public:
		int buffer_frames = 0;
		float one_over_buffer_frames = 0;
		float sample_rate_adjustment = 1;
		int n_channels = 0;
		int samplerate = 48000;
		double one_over_samplerate = 1;
	};

	template <typename T, typename SigType = double>
	struct gf_i_buffer_reader
	{
	public:
		bool (*sample_param_buffer)(T* buffer, gf_param* param, int grain_id) = nullptr;
		void (*sample_buffer)(T* buffer, int channel, SigType* __restrict samples, const SigType* positions,
		                      const int size, const float lower_bound, const float upper_bound) = nullptr;
		bool (*update_buffer_info)(T* buffer, const gf_io_config<SigType>& io_config, gf_buffer_info* buffer_info) =
			nullptr;
		void (*sample_envelope)(T* buffer, const bool use_default, const int n_envelopes, const float env2d_pos,
		                        SigType* __restrict samples, const SigType* __restrict grain_clock,
		                        const int size) = nullptr;
		void (*write_buffer)(T* buffer, const int channel, const SigType* samples,
		                     const int start_position, const int size) = nullptr;
		void (*read_buffer)(T* buffer, int channel, SigType* __restrict samples, int start_sample,
		                    const int size) = nullptr;
		void (*clear_buffer)(T* buffer) = nullptr;
	};
}
