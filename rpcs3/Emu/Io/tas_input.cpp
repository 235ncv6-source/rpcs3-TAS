#include "stdafx.h"
#include "tas_input.h"

#include "Utilities/File.h"

#include <array>
#include <cstdlib>
#include <mutex>
#include <type_traits>

LOG_CHANNEL(tas_log, "TAS");

namespace tas_input
{
	namespace
	{
		enum class tas_mode
		{
			off,
			record,
			playback,
		};

		struct tas_file_header
		{
			std::array<char, 8> magic{};
			u32 version = 1;
			u32 record_size = 0;
		};

		struct tas_pad_record
		{
			u64 read_index = 0;
			u32 port_no = 0;
			CellPadData data{};
		};

		static_assert(std::is_trivially_copyable_v<CellPadData>);
		static_assert(std::is_trivially_copyable_v<tas_file_header>);
		static_assert(std::is_trivially_copyable_v<tas_pad_record>);

		constexpr std::array<char, 8> tas_magic{ 'R', 'P', 'C', 'S', '3', 'T', 'A', 'S' };
		constexpr u32 tas_version = 1;

		class tas_pad_stream
		{
		public:
			void handle(u32 port_no, CellPadData& data)
			{
				std::lock_guard lock(m_mutex);

				if (!m_initialized)
				{
					initialize();
				}

				switch (m_mode)
				{
				case tas_mode::record:
					record(port_no, data);
					break;
				case tas_mode::playback:
					playback(port_no, data);
					break;
				case tas_mode::off:
				default:
					break;
				}
			}

		private:
			void initialize()
			{
				m_initialized = true;

				if (const char* path = std::getenv("RPCS3_TAS_PLAYBACK"); path && path[0])
				{
					m_file.open(path, fs::read + fs::isfile);

					if (!m_file)
					{
						tas_log.error("Failed to open TAS playback file: '%s'", path);
						return;
					}

					tas_file_header header{};
					if (m_file.read(&header, sizeof(header)) != sizeof(header) || header.magic != tas_magic || header.version != tas_version || header.record_size != sizeof(tas_pad_record))
					{
						tas_log.error("Invalid TAS playback file: '%s'", path);
						m_file.close();
						return;
					}

					m_mode = tas_mode::playback;
					m_path = path;
					tas_log.success("TAS playback started: '%s'", m_path.c_str());
					return;
				}

				if (const char* path = std::getenv("RPCS3_TAS_RECORD"); path && path[0])
				{
					m_file.open(path, fs::rewrite);

					if (!m_file)
					{
						tas_log.error("Failed to open TAS recording file: '%s'", path);
						return;
					}

					const tas_file_header header{ tas_magic, tas_version, sizeof(tas_pad_record) };
					if (m_file.write(&header, sizeof(header)) != sizeof(header))
					{
						tas_log.error("Failed to write TAS recording header: '%s'", path);
						m_file.close();
						return;
					}

					m_mode = tas_mode::record;
					m_path = path;
					tas_log.success("TAS recording started: '%s'", m_path.c_str());
				}
			}

			void record(u32 port_no, const CellPadData& data)
			{
				const tas_pad_record record
				{
					.read_index = m_read_index++,
					.port_no = port_no,
					.data = data,
				};

				if (m_file.write(&record, sizeof(record)) != sizeof(record))
				{
					tas_log.error("TAS recording write failed at input read %llu. Disabling TAS recording.", m_read_index - 1);
					m_file.close();
					m_mode = tas_mode::off;
				}
			}

			void playback(u32 port_no, CellPadData& data)
			{
				tas_pad_record record{};

				if (m_file.read(&record, sizeof(record)) != sizeof(record))
				{
					tas_log.error("TAS playback reached end of file at input read %llu. Disabling TAS playback.", m_read_index);
					m_file.close();
					m_mode = tas_mode::off;
					return;
				}

				if (record.read_index != m_read_index || record.port_no != port_no)
				{
					tas_log.error("TAS playback desync at input read %llu: expected read=%llu port=%d, file has read=%llu port=%d. Disabling TAS playback.",
						m_read_index, m_read_index, port_no, record.read_index, record.port_no);
					m_file.close();
					m_mode = tas_mode::off;
					return;
				}

				m_read_index++;
				data = record.data;
			}

			std::mutex m_mutex;
			bool m_initialized = false;
			tas_mode m_mode = tas_mode::off;
			fs::file m_file{};
			std::string m_path;
			u64 m_read_index = 0;
		};

		tas_pad_stream g_tas_pad_stream;
	}

	void handle_pad_data(u32 port_no, CellPadData& data)
	{
		g_tas_pad_stream.handle(port_no, data);
	}
}
