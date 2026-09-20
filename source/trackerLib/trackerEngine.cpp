#include "trackerEngine.h"

#include <atomic>
#include <cstring>

#include "ft2/tm_ft2.h"
#include "schism/tm_schism.h"

namespace trackerLib
{
	namespace
	{
		std::atomic<bool> g_ft2Taken{false};

		class Ft2Engine final : public Engine
		{
		public:
			explicit Ft2Engine(const bool _mod) : m_mod(_mod) {}
			~Ft2Engine() override
			{
				tmFt2Unload();
				g_ft2Taken = false;
			}

			void setSampleRate(const uint32_t _rate) override	{ tmFt2SetSampleRate(_rate); }
			void setSinc512(const bool _enabled) override		{ tmFt2SetSinc512(_enabled); }
			void setTempoScale(const double _scale) override	{ tmFt2SetTempoScale(_scale); }

			void play(const int _order, const bool _stopAtEnd) override { tmFt2Play(_order, _stopAtEnd); }
			void stop() override								{ tmFt2Stop(); }
			void render(float* _out, const uint32_t _frames) override { tmFt2Render(_out, _frames); }

			bool hasEnded() const override			{ return tmFt2HasEnded(); }
			int getOrder() const override			{ return tmFt2GetOrder(); }
			int getRow() const override				{ return tmFt2GetRow(); }
			int getOrderCount() const override		{ return tmFt2GetOrderCount(); }
			int getChannelCount() const override	{ return tmFt2GetChannelCount(); }
			int getInitialBpm() const override		{ return tmFt2GetInitialBpm(); }
			float getChannelLevel(const int _ch) override { return tmFt2GetChannelLevel(_ch); }
			std::string getTitle() const override	{ return tmFt2GetTitle(); }
			bool isAmigaPanned() const override		{ return m_mod; }

		private:
			bool m_mod;
		};

		class SchismEngine final : public Engine
		{
		public:
			explicit SchismEngine(tm_schism_t* _song) : m_song(_song) {}
			~SchismEngine() override { tmSchismDestroy(m_song); }

			void setSampleRate(const uint32_t _rate) override	{ tmSchismSetSampleRate(m_song, _rate); }
			void setSinc512(const bool _enabled) override		{ tmSchismSetSinc512(m_song, _enabled); }
			void setTempoScale(const double _scale) override	{ tmSchismSetTempoScale(m_song, _scale); }

			void play(const int _order, const bool _stopAtEnd) override { tmSchismPlay(m_song, _order, _stopAtEnd); }
			void stop() override								{ tmSchismStop(m_song); }
			void render(float* _out, const uint32_t _frames) override { tmSchismRender(m_song, _out, _frames); }

			bool hasEnded() const override			{ return tmSchismHasEnded(m_song); }
			int getOrder() const override			{ return tmSchismGetOrder(m_song); }
			int getRow() const override				{ return tmSchismGetRow(m_song); }
			int getOrderCount() const override		{ return tmSchismGetOrderCount(m_song); }
			int getChannelCount() const override	{ return tmSchismGetChannelCount(m_song); }
			int getInitialBpm() const override		{ return tmSchismGetInitialBpm(m_song); }
			float getChannelLevel(const int _ch) override { return tmSchismGetChannelLevel(m_song, _ch); }
			std::string getTitle() const override	{ return tmSchismGetTitle(m_song); }

		private:
			tm_schism_t* m_song;
		};

		bool isS3m(const std::vector<uint8_t>& _d)
		{
			return _d.size() > 48 && std::memcmp(&_d[44], "SCRM", 4) == 0;
		}

		bool isIt(const std::vector<uint8_t>& _d)
		{
			return _d.size() > 192 && std::memcmp(_d.data(), "IMPM", 4) == 0;
		}

		bool isXm(const std::vector<uint8_t>& _d)
		{
			return _d.size() > 60 && std::memcmp(_d.data(), "Extended Module: ", 17) == 0;
		}
	}

	bool isModuleExtension(const std::string& _ext)
	{
		return _ext == "xm" || _ext == "s3m" || _ext == "it" || _ext == "mod";
	}

	std::unique_ptr<Engine> createEngine(const std::vector<uint8_t>& _data, const uint32_t _sampleRate)
	{
		if(isS3m(_data) || isIt(_data))
		{
			auto* song = tmSchismCreate(_data.data(), _data.size(), _sampleRate);
			if(!song)
				return {};
			return std::make_unique<SchismEngine>(song);
		}

		if(g_ft2Taken.exchange(true))
			return {};

		tmFt2SetSampleRate(_sampleRate);

		const bool mod = !isXm(_data);

		if(!tmFt2Load(_data.data(), _data.size(), mod ? TM_FT2_FORMAT_MOD : TM_FT2_FORMAT_XM))
		{
			g_ft2Taken = false;
			return {};
		}

		return std::make_unique<Ft2Engine>(mod);
	}
}
