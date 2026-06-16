#pragma once 
#include "gfUtils.h"
#include "gfEnvelopes.h"
#include <vector>
#ifndef M_PI
	#define _USE_MATH_DEFINES
	#include <cmath>
#endif 
#ifndef PI
constexpr double PI = M_PI;
#endif
#ifndef TWOPI
constexpr  double TWOPI = M_PI * 2;
#endif

namespace Grainflow
{
	class GfSyn
	{
	private:
	public:
		template <long INTERNALBLOCK>
		static inline bool Phasor(const double* freqs, double** __restrict outputs, const long blockSize,
		                          const long channels, const double oneOverSamplerate, double* __restrict history)
		{
			if (blockSize < INTERNALBLOCK) return false;
			long blocks = blockSize / INTERNALBLOCK;
			//This assumes the same frequency over the entire block
			for (long ch = 0; ch < channels; ++ch)
			{
				for (long i = 0; i < blocks; ++i)
				{
					double rate = freqs[ch] * oneOverSamplerate;
					double base = history[ch];
					double* result = &outputs[ch][INTERNALBLOCK * i];
					PhasorWave<double, INTERNALBLOCK>(result, rate, history[ch], 0);
				}
			}
			return true;
		};

		template <long INTERNALBLOCK>
		static inline bool SineTable(const double* freqs, double** __restrict outputs, const long blockSize,
		                             const long channels, const double oneOverSamplerate, double* __restrict positions,
		                             double* __restrict extra, double* __restrict history)
		{
			if (blockSize < INTERNALBLOCK) return false;
			long blocks = blockSize / INTERNALBLOCK;
			//This assumes the same frequency over the entire block
			for (long ch = 0; ch < channels; ch++)
			{
				for (long i = 0; i < blocks; i++)
				{
					double rate = freqs[ch] * oneOverSamplerate;
					double* result = &outputs[ch][INTERNALBLOCK * i];
					PhasorWave<double, INTERNALBLOCK>(positions, rate, history[ch], 0);
					GfSyn::ReadQuarterTable<double, INTERNALBLOCK>(gf_envelopes::quarter_sine_wave, result, positions,
					                                               extra, 4096);
				}
			}
			return true;
		}

		template <long INTERNALBLOCK>
		static inline bool ChevySine(const double* freqs, double** __restrict outputs, const long blockSize,
		                             const long channels, const double oneOverSamplerate, double* __restrict positions,
		                             double* __restrict history)
		{
			if (blockSize < INTERNALBLOCK) return false;
			long blocks = blockSize / INTERNALBLOCK;
			//This assumes the same frequency over the entire block
			for (long ch = 0; ch < channels; ch++)
			{
				for (long i = 0; i < blocks; i++)
				{
					double rate = freqs[ch] * oneOverSamplerate;
					double* result = &outputs[ch][INTERNALBLOCK * i];
					PhasorWave<double, INTERNALBLOCK>(positions, rate, history[ch], -0.5);
					ChevyshevSin<double, INTERNALBLOCK>(result, positions);
				}
			}
			return true;
		}

		template <typename T, long INTERNALBLOCK>
		static inline void PhasorWave(T* __restrict result, const T rate, T& __restrict base, const T offset = 0)
		{
			if (base + rate * (INTERNALBLOCK - 1) < 1.0)
			{
				for (long j = 0; j < INTERNALBLOCK; ++j)
				{
					result[j] = base + rate * j + offset;
				}
			}
			else
			{
				for (long j = 0; j < INTERNALBLOCK; ++j)
				{
					result[j] = gf_utils::mod<T>(base + rate * j) + offset;
				}
			}
			base = gf_utils::mod<T>(base + rate * (INTERNALBLOCK));
		}

		template <typename T, long INTERNALBLOCK>
		static inline void PhasorWave(int* __restrict result, const T rate, T& __restrict base, const int size)
		{
			if (base + rate * (INTERNALBLOCK - 1) < 1.0)
			{
				for (long j = 0; j < INTERNALBLOCK; ++j)
				{
					auto pos = base + rate * j;
					result[j] = (int)(pos * size);
				}
			}
			else
			{
				for (long j = 0; j < INTERNALBLOCK; ++j)
				{
					auto pos = gf_utils::mod(base + rate * j);
					result[j] = (int)(pos * size);
				}
			}
			base = gf_utils::mod(base + rate * (INTERNALBLOCK));
		}

		template <typename T, long INTERNALBLOCK>
		static inline void TriangleWave(T* __restrict result, const T rate, T& __restrict base)
		{
			PhasorWave<T, INTERNALBLOCK>(result, rate, base);
			for (long j = 0; j < INTERNALBLOCK; ++j)
			{
				result[j] = (std::abs((result[j] - 0.5) * 2) - 0.5) * 2;
			}
		}


		template <typename T, long INTERNALBLOCK>
		static inline void ReadTable(const T* __restrict table, T* __restrict result, const int* __restrict positions)
		{
			for (long i = 0; i < INTERNALBLOCK; ++i)
			{
				result[i] = table[positions[i]];
			}
		}

		template <typename T, long INTERNALBLOCK>
		static inline void ReadTableLerp(const T* __restrict table, T* __restrict result,
		                                 const float* __restrict positions)
		{
			for (long i = 0; i < INTERNALBLOCK; ++i)
			{
				int p1 = std::floor(positions[i]);
				int p2 = p1 + 1;
				float a = positions[i] - p1;

				result[i] = gf_utils::lerp(table[p1], table[p2], a);
			}
		}

		template <typename T, long INTERNALBLOCK>
		static inline void ReadQuarterTable(const T* __restrict table, T* __restrict result, T* __restrict positions,
		                                    T* __restrict extra, const int size)
		{
			for (long i = 0; i < INTERNALBLOCK; ++i)
			{
				auto tri = std::abs((positions[i] - 0.5) * 2);
				extra[i] = (1 - std::abs((tri - 0.5) * 2)) * size;
			}
			for (long i = 0; i < INTERNALBLOCK; ++i)
			{
				result[i] = table[(int)extra[i]] * (1 - 2 * (positions[i] > 0.5));
			}
		}


		template <typename T, long INTERNALBLOCK>
		static inline void Onepole(T* __restrict result, const T freq, const T onOverSamplerate, T& __restrict history)
		{
			auto coef = std::exp(onOverSamplerate * freq * -TWOPI);
			for (long j = 0; j < INTERNALBLOCK / 4; ++j)
			{
				auto thisResult = &result[j * 4];
				Onepole4(thisResult, coef, history);
			}
		}


		template <typename T, long INTERNALBLOCK>
		static inline void ChevyshevSin(T* __restrict result, const T* __restrict positions)
		{
			//https://web.archive.org/web/20200628195036/http://mooooo.ooo/chebyshev-sine-approximation/ 
			constexpr T coefs[6] = {
				-0.10132118, // x
				0.0066208798, // x^3
				-0.00017350505, // x^5
				0.0000025222919, // x^7
				//-0.000000023317787, // x^9
				//0.00000000013291342, // x^11
			};
			const T PI_MINOR = -0.00000008742278;

			for (int i = 0; i < INTERNALBLOCK; ++i)
			{
				auto x = (positions[i]) * TWOPI;
				auto x2 = x * x;
				//auto p11 = coefs[5];
				//auto p9 = p11 * x2 + coefs[4];
				//auto p7 = p9 * x2 + coefs[3];
				auto p7 = coefs[3];
				auto p5 = p7 * x2 + coefs[2];
				auto p3 = p5 * x2 + coefs[1];
				auto p1 = p3 * x2 + coefs[0];
				result[i] = (x - PI - PI_MINOR) * (x + PI + PI_MINOR) * p1 * x;
			}
		}
	};

	template <typename T, long INTERNALBLOCK>
	class phasor
	{
	private:
		T history_{0};
		T rate_{1};

	public:
		phasor(T rate, int samplerate)
		{
			set_rate(rate, samplerate);
		}


		inline void set_rate(T rate, int samplerate)
		{
			rate_ = rate / samplerate;
		}

		inline void set_rate(T rate, int samplerate, int file_samples, int file_samplerate)
		{
			rate_ = rate * file_samplerate / (file_samples * samplerate);
		}

		void perform(T* buffer, const int frames = INTERNALBLOCK)
		{
			const int tiles = frames/INTERNALBLOCK;
			for (int i = 0; i < tiles; ++i){
				auto *pin = buffer+(i*INTERNALBLOCK);
				GfSyn::PhasorWave<T, INTERNALBLOCK>(pin, rate_, history_);
			}
		}
	};
}
