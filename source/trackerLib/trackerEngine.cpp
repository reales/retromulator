#include "trackerEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#endif

#include "tmParallel.h"
#include "ft2/tm_ft2.h"
#include "schism/tm_schism.h"

namespace trackerLib
{
	namespace
	{
		std::atomic<bool> g_ft2Taken{false};

		void cpuRelax()
		{
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
			_mm_pause();
#elif defined(__aarch64__) && !defined(_MSC_VER)
			__asm__ volatile("yield");
#else
			std::this_thread::yield();
#endif
		}

		// Mixes the voices of one tick in parallel. Only used by the offline render,
		// threads start on the first job and live as long as any engine does.
		class Pool
		{
		public:
			~Pool()
			{
				{
					std::lock_guard lock(m_mutex);
					m_quit = true;
					++m_generation;
				}
				m_wake.notify_all();
				for(auto& t : m_threads)
					t.join();
			}

			void run(const int32_t _count, const tmParallelFn _fn, void* _ctx)
			{
				std::unique_lock run(m_runMutex, std::try_to_lock);

				if(_count < 2 || !run.owns_lock() || !start())
				{
					for(int32_t i = 0; i < _count; ++i)
						_fn(i, 0, _ctx);
					return;
				}

				Job job{_fn, _ctx, _count};
				{
					std::lock_guard lock(m_mutex);
					m_job = &job;
					++m_generation;
				}
				m_wake.notify_all();

				work(job, 0);

				std::unique_lock lock(m_mutex);
				m_job = nullptr;
				m_done.wait(lock, [this] { return m_inJob == 0; });
			}

		private:
			struct Job
			{
				tmParallelFn fn;
				void* ctx;
				int32_t count;
				std::atomic<int32_t> next{0};
			};

			static void work(Job& _job, const int32_t _worker)
			{
				for(;;)
				{
					const auto i = _job.next.fetch_add(1);
					if(i >= _job.count)
						return;
					_job.fn(i, _worker, _job.ctx);
				}
			}

			bool start()
			{
				if(!m_started)
				{
					m_started = true;
					const auto total = std::clamp<int32_t>(static_cast<int32_t>(std::thread::hardware_concurrency()), 1, TM_PARALLEL_MAX_WORKERS);
					for(int32_t w = 1; w < total; ++w)
						m_threads.emplace_back([this, w] { workerMain(w); });
				}
				return !m_threads.empty();
			}

			void workerMain(const int32_t _worker)
			{
				uint64_t seen = 0;

				for(;;)
				{
					// ticks follow each other closely during a render: spin before sleeping
					const auto spinEnd = std::chrono::steady_clock::now() + std::chrono::microseconds(200);
					while(m_generation.load(std::memory_order_relaxed) == seen && std::chrono::steady_clock::now() < spinEnd)
					{
						for(int i = 0; i < 32; ++i)
							cpuRelax();
					}

					Job* job;
					{
						std::unique_lock lock(m_mutex);
						m_wake.wait(lock, [&] { return m_generation.load() != seen; });
						seen = m_generation.load();
						if(m_quit)
							return;
						job = m_job;
						if(job)
							++m_inJob;
					}

					if(!job)
						continue;

					work(*job, _worker);

					std::lock_guard lock(m_mutex);
					if(--m_inJob == 0)
						m_done.notify_one();
				}
			}

			std::mutex m_runMutex;	// one job at a time
			std::mutex m_mutex;
			std::condition_variable m_wake, m_done;
			std::vector<std::thread> m_threads;
			std::atomic<uint64_t> m_generation{0};
			Job* m_job = nullptr;
			int32_t m_inJob = 0;
			bool m_started = false;
			bool m_quit = false;
		};

		std::mutex g_poolMutex;
		std::weak_ptr<Pool> g_pool;

		std::shared_ptr<Pool> acquirePool()
		{
			std::lock_guard lock(g_poolMutex);
			auto pool = g_pool.lock();
			if(!pool)
			{
				pool = std::make_shared<Pool>();
				g_pool = pool;
			}
			return pool;
		}

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
			std::shared_ptr<Pool> m_pool = acquirePool();
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
			std::shared_ptr<Pool> m_pool = acquirePool();
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

extern "C" void tmParallelFor(const int32_t _count, const tmParallelFn _fn, void* _ctx)
{
	std::shared_ptr<trackerLib::Pool> pool;
	{
		std::lock_guard lock(trackerLib::g_poolMutex);
		pool = trackerLib::g_pool.lock();
	}

	if(pool)
	{
		pool->run(_count, _fn, _ctx);
		return;
	}

	for(int32_t i = 0; i < _count; ++i)
		_fn(i, 0, _ctx);
}
