#pragma once
// sk: <map> dropped (unused upstream). <string> kept only for the unused reflection helpers, which stay
// unreferenced (we drive the collection via the enum API, not the string API) and get elided.
#include <string>
#include <algorithm>
#include "gfUtils.h"

namespace Grainflow
{
	/// <summary>
	/// Available parameters using the GfParam struct
	/// </summary>
	enum class gf_param_name : std::uint8_t
	{
		ERR = 0,
		delay,
		rate,
		glisson,
		glisson_rows,
		glisson_position,
		window,
		amplitude,
		space,
		envelope_position,
		n_envelopes,
		direction,
		start_point,
		stop_point,
		rate_quantize_semi,
		loop_mode,
		channel,
		density,
		vibrato_rate,
		vibrato_depth,
		//These do not have param structs
		transpose,
		glisson_st,
		stream,
	};

	/// <summary>
	/// Different parameter types using in the GfParam Struct
	/// </summary>
	enum class gf_param_type : std::uint8_t
	{
		ERR = 0,
		base,
		random,
		offset,
		mode,
		value,
	};

	enum class gf_random_mode : std::uint8_t
	{
		bipolar = 0,
		positive = 1,
		negative = 2,
	};

	struct gf_value_table
	{
		float delay;
		float rate;
		float glisson;
		float window;
		float amplitude;
		float space;
		float envelopePosition;
		float direction;
		int density;
	};

	/// <summary>
/// Parameter entity. When used with GfParamSet() different fields can be set
/// SampleParam() is used to set the value field which is what should be used to read the correct value
/// </summary>


	enum class gf_stream_set_type : std::uint8_t
	{
		automatic_streams = 0,
		per_streams,
		random_streams,
		manual_streams,
	};

	enum class gf_buffer_mode : std::uint8_t
	{
		normal = 0,
		buffer_sequence = 1,
		buffer_random = 2,
	};


	enum class gf_buffers : std::uint8_t
	{
		buffer = 0,
		envelope,
		rate_buffer,
		delay_buffer,
		window_buffer,
		glisson_buffer,
	};

	struct gf_param
	{
	public:
		float base = 0;
		float random = 0;
		float offset = 0;
		float value = 0;
		gf_buffer_mode mode = gf_buffer_mode::normal;

		void sample(int offset_id = 0, gf_random_mode random_mode = gf_random_mode::bipolar)
		{
			float random_value = 0.0f;
			switch (random_mode)
			{
			case gf_random_mode::bipolar:
				random_value = gf_utils::deviate(0, random);
				break;
			case gf_random_mode::negative:
				random_value = gf_utils::random_range(0, -random);
				break;
			case gf_random_mode::positive:
				random_value = gf_utils::random_range(0, random);
				break;
			}

			value = base + offset * offset_id + random_value;
		}
	};

	static inline bool buffer_reflection(std::string reflectionString, gf_buffers& type) // sk: +inline (header)
	{
		if (reflectionString == "buf" || reflectionString == "buffer")
		{
			type = gf_buffers::buffer;
		}
		else if (reflectionString == "env" || reflectionString == "envelope")
		{
			type = gf_buffers::envelope;
		}
		else if (reflectionString == "delay" || reflectionString == "delays" || reflectionString == "delayBuffer")
		{
			type = gf_buffers::delay_buffer;
		}
		else if (reflectionString == "window" || reflectionString == "windows" || reflectionString == "windowBuffer")
		{
			type = gf_buffers::window_buffer;
		}
		else if (reflectionString == "glisson" || reflectionString == "glissonBuffer")
		{
			type = gf_buffers::glisson_buffer;
		}
		else if (reflectionString == "rate" || reflectionString == "rates" || reflectionString == "rateBuffer")
		{
			type = gf_buffers::rate_buffer;
		}
		else
		{
			return false;
		}
		return true;
	}


	static inline bool param_reflection(std::string param, gf_param_name& out_param_name, gf_param_type& out_param_type) // sk: +inline (header)
	{
		//Find and remove param types 
		out_param_type = gf_param_type::ERR;
		if (const auto pos = param.find("Random"); pos != std::string::npos)
		{
			out_param_type = gf_param_type::random;
			param.erase(pos, 6);
		}
		else if (const auto pos = param.find("Offset"); pos != std::string::npos)
		{
			out_param_type = gf_param_type::offset;
			param.erase(pos, 6);
		}
		else if (const auto pos = param.find("Mode"); pos != std::string::npos)
		{
			out_param_type = gf_param_type::mode;
			param.erase(pos, 4);
		}
		else out_param_type = gf_param_type::base;
		if (out_param_type == gf_param_type::ERR) return false;

		out_param_name = gf_param_name::ERR;
		if (param == "delay") { out_param_name = gf_param_name::delay; }
		else if (param == "rate") { out_param_name = gf_param_name::rate; }
		else if (param == "window") { out_param_name = gf_param_name::window; }
		else if (param == "rate") { out_param_name = gf_param_name::rate; }
		else if (param == "amp") { out_param_name = gf_param_name::amplitude; }
		else if (param == "space") { out_param_name = gf_param_name::space; }
		else if (param == "envelopePosition") { out_param_name = gf_param_name::envelope_position; }
		else if (param == "direction") { out_param_name = gf_param_name::direction; }
		else if (param == "startPoint") { out_param_name = gf_param_name::start_point; }
		else if (param == "stopPoint") { out_param_name = gf_param_name::stop_point; }
		else if (param == "rateQuantizeSemi") { out_param_name = gf_param_name::rate_quantize_semi; }
		else if (param == "loopMode") { out_param_name = gf_param_name::loop_mode; }
		else if (param == "channel") { out_param_name = gf_param_name::channel; }
		else if (param == "density") { out_param_name = gf_param_name::density; }
		else if (param == "vibratoDepth") { out_param_name = gf_param_name::vibrato_depth; }
		else if (param == "vibratoRate") { out_param_name = gf_param_name::vibrato_rate; }
		//These cases are converted internally to other parameters 
		else if (param == "transpose") { out_param_name = gf_param_name::transpose; }
		else if (param == "glissonSt") { out_param_name = gf_param_name::glisson_st; }
		else if (param == "stream") { out_param_name = gf_param_name::stream; }
		else if (param == "nEnvelopes")
		{
			out_param_name = gf_param_name::n_envelopes;
			out_param_type = gf_param_type::value;
		}


		if (out_param_name == gf_param_name::ERR) return false;

		return true;
	};
}
