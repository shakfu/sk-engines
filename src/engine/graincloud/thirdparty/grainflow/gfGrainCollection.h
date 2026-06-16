#pragma once
// sk-engines port: de-STL'd. The grain array was a std::unique_ptr<gf_grain[]> (heap); it is now a raw
// pointer into caller-provided storage (the SDRAM arena), set via set_storage() before resize(). No
// heap on the audio path. Changes vs upstream are marked "// sk:".
#include "gfGrain.h"
#include "gfParam.h"

namespace Grainflow
{
	template <typename T, size_t Internalblock, typename SigType = double>
	class gf_grain_collection
	{
	private:
		gf_grain<T, Internalblock, SigType>* grains_ = nullptr; // sk: was unique_ptr; points to arena storage
		int storage_capacity_ = 0;                              // sk: max grains the storage holds
		gf_i_buffer_reader<T, SigType> buffer_reader_;
		int grain_count_ = 0;
		int active_grains_ = 0;
		int nstreams_ = 0;
		bool auto_overlap_ = true;

	public:
		int samplerate = 48000;

		explicit gf_grain_collection(gf_i_buffer_reader<T, SigType> buffer_reader, int grain_count = 0);

		~gf_grain_collection();

		// sk: caller hands the collection its grain storage (an array of `capacity` default-constructed
		// gf_grain placement-new'd in the SDRAM arena). Must be called before resize(). No heap here.
		void set_storage(gf_grain<T, Internalblock, SigType>* storage, int capacity)
		{
			grains_ = storage;
			storage_capacity_ = capacity;
		}

		void resize(int grain_count);

		[[nodiscard]] int grains() const;

		gf_grain<T, Internalblock, SigType>* get_grain(int index);


#pragma region DSP
		// Processes all grain given an io config with the correct inputs and outputs  
		void process(gf_io_config<SigType>& io_config);

#pragma endregion

#pragma region Params

		static void transform_params(gf_param_name& param_name, const gf_param_type& param_type, float& value);

		void param_set(int target, gf_param_name param_name, gf_param_type param_type, float value);

		GF_RETURN_CODE param_set(int target, const std::string& reflection_string, float value);

		void channel_param_set(int channel, gf_param_name param_name, gf_param_type param_type, float value);

		GF_RETURN_CODE channel_param_set(int channel, const std::string& reflection_string, float value);

		GF_RETURN_CODE grain_param_func(gf_param_name param_name, gf_param_type param_type,
		                                float (*func)(float, float, float),
		                                float a, float b);

		GF_RETURN_CODE grain_param_func(const std::string& reflection_string, float (*func)(float, float, float),
		                                float a,
		                                float b);

		float param_get(int target, gf_param_name param_name);

		float param_get(int target, gf_param_name param_name, gf_param_type param_type);

		void set_active_grains(int n_grains);

		[[nodiscard]] int active_grains() const;

		void set_auto_overlap(bool auto_overlap);
		bool get_auto_overlap();
		GF_RETURN_CODE set_buffer(gf_buffers type, T* ref, int target);
		GF_RETURN_CODE set_buffer(std::string reflectionString, T* ref, int target);

#pragma endregion

#pragma region STREAMS

		[[nodiscard]] int streams() const;

		GF_RETURN_CODE stream_param_set(int stream, gf_param_name param_name, gf_param_type param_type, float value);

		GF_RETURN_CODE stream_param_set(const std::string& reflection_string, int stream, float value);

		GF_RETURN_CODE stream_param_func(gf_param_name param_name, gf_param_type param_type,
		                                 float (*func)(float, float, float),
		                                 float a, float b);

		GF_RETURN_CODE stream_param_func(const std::string& reflection_string, float (*func)(float, float, float),
		                                 float a,
		                                 float b);

		void stream_set(gf_stream_set_type mode, int nstreams);

		void stream_set(int grain, int stream_id);

		int stream_get(int grain_index);;

#pragma endregion

		T* get_buffer(gf_buffers type, int index = 0);

		int chanel_get(int index);

		void channels_set_interleaved(int channels);

		void channel_set(int index, int channel);

		void channel_mode_set(int mode);
	};

	template <typename T, size_t Internalblock, typename SigType>
	gf_grain_collection<T, Internalblock, SigType>::gf_grain_collection(gf_i_buffer_reader<T, SigType> buffer_reader,
	                                                                    const int grain_count)
	{
		this->buffer_reader_ = buffer_reader;
		if (grain_count > 0)
		{
			resize(grain_count);
		}
	}

	template <typename T, size_t Internalblock, typename SigType>
	gf_grain_collection<T, Internalblock, SigType>::~gf_grain_collection()
	{
		// sk: storage is arena-owned by the caller (engine), not by the collection. Nothing to free.
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::resize(const int grain_count)
	{
		// sk: no heap alloc. Storage is the arena array set via set_storage(); clamp to its capacity and
		// (re)initialise the live grains in place. The grains are already constructed (placement-new'd).
		if (grains_ == nullptr) return;
		grain_count_ = grain_count < storage_capacity_ ? grain_count : storage_capacity_;
		for (int i = 0; i < grain_count_; i++)
		{
			grains_[i].buffer_reader = buffer_reader_;
			grains_[i].set_index(i);
			grains_[i].system_samplerate = samplerate;
		}
		set_active_grains(grain_count_); // sk: use the clamped count
	}

	template <typename T, size_t Internalblock, typename SigType>
	int gf_grain_collection<T, Internalblock, SigType>::grains() const
	{
		return grain_count_;
	}

	template <typename T, size_t Internalblock, typename SigType>
	gf_grain<T, Internalblock, SigType>* gf_grain_collection<T, Internalblock, SigType>::get_grain(int index)
	{
		if (index >= grain_count_) return nullptr;
		return &grains_[index];
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::process(gf_io_config<SigType>& io_config)
	{
		for (int g = 0; g < grain_count_; g++)
		{
			grains_[g].process(io_config);
		}
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::transform_params(gf_param_name& param_name,
	                                                                      const gf_param_type& param_type,
	                                                                      float& value)
	{
		if (param_type == gf_param_type::mode) return; //Modes are not a value type and should not be effected 
		switch (param_name)
		{
		case gf_param_name::transpose:
			if (param_type == gf_param_type::base) value = gf_utils::pitch_to_rate(value);
			else value = gf_utils::pitch_offset_to_rate_offset(value);
			param_name = gf_param_name::rate;
			break;
		case gf_param_name::glisson_st:
			value = gf_utils::pitch_offset_to_rate_offset(value);
			param_name = gf_param_name::glisson;
			break;
		case gf_param_name::amplitude:
			if (param_type == gf_param_type::base) break;
			value = std::max(std::min(-value, 0.0f), -1.0f);
			break;
		default:
			break;
		}
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::set_buffer(gf_buffers type, T* ref, int target)
	{
		if (target == 0)
		{
			for (int g = 0; g < grain_count_; g++)
			{
				grains_[g].set_buffer(type, ref);
			}
			return GF_RETURN_CODE::GF_SUCCESS;
		}
		if (target > grains()) return GF_RETURN_CODE::GF_ERR;
		grains_[target - 1].set_buffer(type, ref);
		return GF_RETURN_CODE::GF_SUCCESS;
	};

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::set_buffer(
		std::string reflectionString, T* ref, int target)
	{
		gf_buffers type;
		if (!buffer_reflection(reflectionString, type)) return GF_RETURN_CODE::GF_ERR;

		return set_buffer(type, ref, target);
	};

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::param_set(int target, gf_param_name param_name,
	                                                               gf_param_type param_type,
	                                                               float value)
	{
		if (target > grain_count_ + 1) { return; }
		if (param_name == gf_param_name::stream)
		{
			if (target < 1) { return; }
			stream_set(target - 1, static_cast<int>(value));
			return;
		}
		transform_params(param_name, param_type, value);
		if (target <= 0)
		{
			for (int g = 0; g < grain_count_; g++)
			{
				grains_[g].param_set(value, param_name, param_type);
			}
			return;
		}
		grains_[target - 1].param_set(value, param_name, param_type);
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::param_set(const int target,
	                                                                         const std::string& reflection_string,
	                                                                         const float value)
	{
		gf_param_name param_name;
		gf_param_type param_type;
		if (const auto found_reflection = Grainflow::param_reflection(reflection_string, param_name, param_type); !
			found_reflection)
			return GF_RETURN_CODE::GF_PARAM_NOT_FOUND;

		param_set(target, param_name, param_type, value);
		return GF_RETURN_CODE::GF_SUCCESS;
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::channel_param_set(
		const int channel, const gf_param_name param_name,
		const gf_param_type param_type,
		const float value)
	{
		for (int g = 0; g < grain_count_; g++)
		{
			const int grain_chanel = grains_[g].param_get(gf_param_name::channel);
			if (grain_chanel != channel) continue;
			param_set(g + 1, param_name, param_type, value);
		}
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::channel_param_set(
		const int channel, const std::string& reflection_string,
		const float value)
	{
		gf_param_name param_name;
		gf_param_type param_type;
		if (const auto found_reflection = Grainflow::param_reflection(reflection_string, param_name, param_type); !
			found_reflection)
			return GF_RETURN_CODE::GF_PARAM_NOT_FOUND;
		channel_param_set(channel, param_name, param_type, value);
		return GF_RETURN_CODE::GF_SUCCESS;
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::grain_param_func(
		const gf_param_name param_name, const gf_param_type param_type,
		float (*func)(float, float, float), const float a, const float b)
	{
		for (int g = 0; g < grain_count_; g++)
		{
			const float value = (*func)(a, b, static_cast<float>(g) / static_cast<float>(grain_count_));
			param_set(g, param_name, param_type, value);
		}
		return GF_RETURN_CODE::GF_SUCCESS;
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::grain_param_func(
		const std::string& reflection_string,
		float (*func)(float, float, float),
		const float a,
		const float b)
	{
		gf_param_name param_name;
		gf_param_type param_type;
		if (const auto found_reflection = Grainflow::param_reflection(reflection_string, param_name, param_type); !
			found_reflection)
			return GF_RETURN_CODE::GF_PARAM_NOT_FOUND;
		return grain_param_func(param_name, param_type, func, a, b);
	}

	template <typename T, size_t Internalblock, typename SigType>
	float gf_grain_collection<T, Internalblock, SigType>::param_get(const int target, gf_param_name param_name)
	{
		if (target >= grain_count_) return 0;
		if (target <= 1) return grains_[0].param_get(param_name);
		return grains_[target - 1].param_get(param_name);
	}

	template <typename T, size_t Internalblock, typename SigType>
	float gf_grain_collection<T, Internalblock, SigType>::param_get(const int target, gf_param_name param_name,
	                                                                gf_param_type param_type)
	{
		if (target > grain_count_) return 0;
		if (target <= 1) return grains_[0].param_get(param_name, param_type);
		return grains_[target - 1].param_get(param_name, param_type);
	}


	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::set_active_grains(int n_grains)
	{
		if (n_grains <= 0) n_grains = 0;
		else if (n_grains > grain_count_) n_grains = grain_count_;
		active_grains_ = n_grains;
		const auto windowOffset = 1.0f / (n_grains > 0 ? n_grains : 1);
		for (int g = 0; g < grain_count_; g++)
		{
			grains_[g].enabled = g < active_grains_;
			if (auto_overlap_ && grains_[g].enabled)
			{
				param_set(g + 1, gf_param_name::window, gf_param_type::offset, windowOffset);
			}
		}
	}

	template <typename T, size_t Internalblock, typename SigType>
	int gf_grain_collection<T, Internalblock, SigType>::active_grains() const
	{
		return active_grains_;
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::set_auto_overlap(const bool auto_overlap)
	{
		auto_overlap_ = auto_overlap;
		set_active_grains(active_grains_);
	}

	template <typename T, size_t Internalblock, typename SigType>
	bool gf_grain_collection<T, Internalblock, SigType>::get_auto_overlap()
	{
		return auto_overlap_;
	}

	template <typename T, size_t Internalblock, typename SigType>
	int gf_grain_collection<T, Internalblock, SigType>::streams() const
	{
		return nstreams_;
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::stream_param_set(
		int stream, const gf_param_name param_name,
		const gf_param_type param_type,
		const float value)
	{
		if (stream > nstreams_ || stream < 0) return GF_RETURN_CODE::GF_ERR;
		for (int g = 0; g < grain_count_; g++)
		{
			if (grains_[g].stream() != (stream - 1) || stream == 0) continue;
			param_set(g + 1, param_name, param_type, value);
		}
		return GF_RETURN_CODE::GF_SUCCESS;
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::stream_param_set(
		const std::string& reflection_string, const int stream,
		const float value)
	{
		gf_param_name param_name;
		gf_param_type param_type;
		if (const auto found_reflection = Grainflow::param_reflection(reflection_string, param_name, param_type); !
			found_reflection)
			return GF_RETURN_CODE::GF_PARAM_NOT_FOUND;
		return stream_param_set(stream, param_name, param_type, value);
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::stream_param_func(
		const gf_param_name param_name, const gf_param_type param_type,
		float (*func)(float, float, float), const float a, const float b)
	{
		for (int s = 0; s < nstreams_; s++)
		{
			const float value = func(a, b, static_cast<float>(s) / static_cast<float>(nstreams_));
			if (const auto return_code = stream_param_set(s, param_name, param_type, value); return_code !=
				GF_RETURN_CODE::GF_SUCCESS)
				return return_code;
		}
		return GF_RETURN_CODE::GF_SUCCESS;
	}

	template <typename T, size_t Internalblock, typename SigType>
	GF_RETURN_CODE gf_grain_collection<T, Internalblock, SigType>::stream_param_func(
		const std::string& reflection_string,
		float (*func)(float, float, float),
		const float a,
		const float b)
	{
		gf_param_name param_name;
		gf_param_type param_type;
		if (const auto found_reflection = Grainflow::param_reflection(reflection_string, param_name, param_type); !
			found_reflection)
			return GF_RETURN_CODE::GF_PARAM_NOT_FOUND;
		return stream_param_func(param_name, param_type, func, a, b);
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::stream_set(gf_stream_set_type mode, int nstreams)
	{
		nstreams_ = nstreams;
		if (mode == gf_stream_set_type::manual_streams) return;
		for (int g = 0; g < grain_count_; g++)
		{
			grains_[g].stream_set(grain_count_, mode, nstreams);
		}
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::stream_set(const int grain, int stream_id)
	{
		if (grain <= 0) return;
		if (grain > grain_count_) return;
		if (stream_id <= 0) return;
		grains_[grain - 1].stream_set(stream_id, gf_stream_set_type::manual_streams, nstreams_);
	}

	template <typename T, size_t Internalblock, typename SigType>
	int gf_grain_collection<T, Internalblock, SigType>::stream_get(int grain_index)
	{
		return static_cast<int>(grains_[grain_index].stream());
	}

	template <typename T, size_t Internalblock, typename SigType>
	T* gf_grain_collection<T, Internalblock, SigType>::get_buffer(gf_buffers type, int index)
	{
		return grains_[index].get_buffer(type);
	}

	template <typename T, size_t Internalblock, typename SigType>
	int gf_grain_collection<T, Internalblock, SigType>::chanel_get(int index)
	{
		return grains_[index].channel();
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::channels_set_interleaved(const int channels)
	{
		for (int g = 0; g < grain_count_; g++)
		{
			grains_[g].param_set(static_cast<float>(g % channels), gf_param_name::channel, gf_param_type::base);
		}
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::channel_set(int index, const int channel)
	{
		grains_[index].param_set(static_cast<float>(channel), gf_param_name::channel, gf_param_type::base);
	}

	template <typename T, size_t Internalblock, typename SigType>
	void gf_grain_collection<T, Internalblock, SigType>::channel_mode_set(const int mode)
	{
		for (int g = 0; g < grain_count_; g++)
		{
			grains_[g].param_set(static_cast<float>(mode), gf_param_name::channel, gf_param_type::random);
		}
	}
}
